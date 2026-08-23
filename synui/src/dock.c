/*
 * dock.c — macOS-style auto-hide dock
 *
 * Shows pinned and currently-running apps as icons in a bar mirrored on every
 * output. Auto-hide only (no "always visible" mode): hidden, the dock reserves
 * zero layout space; shown, it simply floats above window content (its scene
 * tree sits alongside the welcome/overlay/dispcfg UI trees, not parented under
 * window_tree/layer_tree) — so unlike a layer-shell panel, showing or hiding
 * it never triggers a tiling relayout. The entry model (which apps are
 * pinned/running) is server-global; only the per-output scene tree and its
 * show/hide state live on syn_output::dock.
 *
 * The dock lives on any screen edge (config/state `dock_edge`): BOTTOM/TOP
 * render a horizontal bar, LEFT/RIGHT a vertical column. It can be dragged to
 * a different edge (dock_drag_*), pinned apps are edited live via a right-
 * click context menu (dockmenu_*), and both the edge and the pinned set
 * persist to ~/.config/synui/dock.state.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "contrast.h"
#include "synui.h"

/*
 * ── How big the dock is ──────────────────────────────────
 *
 * `dock_height` is the slab's thickness along the edge normal, and it used to be
 * ONLY that: the icons were a fixed 48px whatever the number said. So the Dock
 * size row turned a 64px bar into a 200px wall of glass with the same small
 * pictures adrift in the middle of it, which is nobody's idea of a bigger dock.
 *
 * The icon is derived from the thickness instead — 16px of slab either side of
 * it, which is exactly what 48-in-64 was — and the padding from the icon. At the
 * stock 64 both come out at the numbers they were literals for, so a desktop
 * that never touches the row is pixel-identical to the one before this.
 *
 * ⚠ NOTHING IN THIS FILE MAY GO BACK TO A CONSTANT. The icon size is now a
 * function of a setting the user can move while looking at the bar, and a stray
 * 48 shows up as a hit box that misses, a dot in the wrong place, or a cached
 * picture at the wrong resolution — all of them silent.
 */
#define DOCK_ICON_INSET 16   /* slab left either side of the icon, cross-axis */

static int dock_icon_size(const syn_config_t *c)
{
    int icon = c->dock_height - DOCK_ICON_INSET;
    /* config.c clamps dock_height to 32..200; these are the same two ends. */
    if (icon < 16)  icon = 16;
    if (icon > 192) icon = 192;
    return icon;
}

/* A sixth of the icon — 8px at stock, and the same proportion at every other
 * size. The gaps are what make a row of icons read as a row rather than as a
 * strip, so they have to grow with what they separate. */
static int dock_icon_pad(const syn_config_t *c)
{
    int pad = dock_icon_size(c) / 6;
    return pad < 4 ? 4 : pad;
}

/* ── Tray-resident apps ──────────────────────────────────────
 *
 * An app that closed to tray has no mapped window, so dock_rebuild() marks it
 * not-running and a click re-runs its .desktop Exec. For most single-instance
 * apps that is right: the second invocation reaches the first, which raises its
 * window.
 *
 * Steam is the documented exception, and it is why this section exists.
 * Measured against a tray-resident Steam, a FRESH client per trial and a fixed
 * 20s settle (see below — both matter):
 *
 *     /usr/bin/steam                    → 1/3   the old click. Flaky, and that
 *                                               "sometimes" is the whole bug.
 *     /usr/bin/steam steam://open/main  → 3/3   window back in ~2s
 *     /usr/bin/steam steam://open/games → 0/4   never
 *
 * Bare `steam` forwards *no instruction* to the running client, so it has
 * nothing to act on; that it works at all is incidental. Steam's tray icon
 * exports no Activate either (see the waybar-tray notes), so no bar can restore
 * it with a plain click — libayatana-appindicator behaviour, not ours.
 *
 * open/main, NOT the [Desktop Action Library] that "right-click → Library"
 * suggests. That was the obvious candidate and it does not work: `open/games`
 * *navigates* a window that already exists, while `open/main` is what *creates*
 * one. Steam's own tray menu can offer Library because it shows the window
 * internally first; the URL cannot.
 *
 * If you re-measure this, control for two things that made three earlier runs
 * of mine say three different things:
 *   - A failed trial leaves Steam already closed, so the next trial's "close"
 *     is a no-op and its command really runs minutes after the close. That
 *     measures the delay, not the command.
 *   - Steam WEDGES: it maps its X window (IsViewable) but never associates a
 *     wl_surface, so there is no buffer, no map, and nothing any compositor can
 *     show. Every trial after that reads as FAIL regardless of command — which
 *     is why the numbers above use a fresh client per trial.
 *
 * The wedge is NOT "after repeated close/restore cycles", and it is NOT beyond
 * the dock — both were earlier readings here, and both were wrong. Caught live
 * 2026-07-16: it formed ~12s into a fresh session with nothing cycled, when
 * Steam destroyed its just-mapped main window during the login→main handoff and
 * the replacement never associated. An X unmap/map recovers it in ~1s.
 *
 * So a tray-restore click now arms dock_arm_unwedge(): if no window has arrived
 * a few seconds later, xwayland_unwedge() rebuilds the surface. open/main is
 * still tried first and still the right thing for a genuinely tray-resident
 * client; the fallback only fires when it demonstrably did nothing.
 */

/*
 * Is a Steam client really live? Mirrors is_steam_running() from Valve's own
 * steam.sh, which is the authority on this and does three things:
 *
 *   1. ~/.steam/steam.pid exists
 *   2. /proc/<pid> exists
 *   3. that process holds ~/.steam/steam.pipe open
 *
 * (3) is the one that matters and the one a naive check misses. Steam does not
 * remove steam.pid on exit — verified: the file outlived a dead client here —
 * so existence proves nothing and even kill(0) can be fooled by PID recycling.
 * Getting this wrong is not harmless: sending a steam:// URL to a dead client
 * silently starts a SECOND Steam whose window never paints. Only claim Steam is
 * up when it is holding its own pipe.
 */
static bool steam_is_running(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s/.steam/steam.pid", home);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    long pid = 0;
    int got = fscanf(f, "%ld", &pid);
    fclose(f);
    if (got != 1 || pid <= 1) return false;

    char pipe_path[512];
    snprintf(pipe_path, sizeof(pipe_path), "%s/.steam/steam.pipe", home);

    /* Sized so "<fd_dir>/<d_name>" cannot truncate: a /proc/<pid>/fd path is
     * ~30 bytes and d_name is at most NAME_MAX. Oversizing fd_dir instead makes
     * the compiler (rightly) warn that the join might not fit. */
    char fd_dir[64];
    snprintf(fd_dir, sizeof(fd_dir), "/proc/%ld/fd", pid);
    DIR *d = opendir(fd_dir);
    if (!d) return false;              /* no such process (or not ours) */

    bool holds_pipe = false;
    struct dirent *de;
    while (!holds_pipe && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char link[64 + 256], target[512];
        snprintf(link, sizeof(link), "%s/%s", fd_dir, de->d_name);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0) continue;          /* raced a closing fd — just skip it */
        target[n] = '\0';
        if (strcmp(target, pipe_path) == 0) holds_pipe = true;
    }
    closedir(d);
    return holds_pipe;
}

/*
 * The command that restores `app_id` from the tray, or NULL if a plain Exec is
 * the right thing — which it is for everything but Steam.
 *
 * Keyed on app_id "steam": that is what synui reports for Steam's main window
 * (verified via synctl — its WM_CLASS is ("steamwebhelper", "steam") and
 * view_app_id() returns the res_class), and it is also what steam.desktop and
 * the dock pin are called, so all three agree and the pinned icon is the same
 * entry as the running one.
 *
 * The steam binary comes from its own .desktop Exec rather than a hardcoded
 * /usr/bin/steam, so a Flatpak/other-prefix install still gets its own
 * launcher. Only the URL is ours, because Steam ships no .desktop action that
 * means "just show the window".
 */
static const char *dock_tray_restore_exec(const char *app_id)
{
    if (strcmp(app_id, "steam") != 0) return NULL;
    if (!steam_is_running()) return NULL;   /* not in the tray — a real launch */

    static char cmd[320];
    const syn_icon_entry_t *ic = icon_lookup("steam");
    snprintf(cmd, sizeof(cmd), "%s steam://open/main",
             ic->exec[0] ? ic->exec : "steam");
    return cmd;
}

/* Auto-hide timing. The dock slides fully in/out over DOCK_SLIDE_SECS; once
 * the cursor leaves it stays put for DOCK_HIDE_DELAY before sliding away, so
 * brushing past the edge doesn't make it flicker.
 *
 * DOCK_REVEAL_DELAY is the same courtesy on the way in: the cursor has to
 * REST in the trigger strip this long before the dock slides out. On a
 * stacked layout the strip along one monitor's dock edge is also the pixel
 * row you cross to reach the neighbouring monitor's bar — without the dwell,
 * aiming at the start button on the screen below pops the dock out on the
 * screen above and the click lands on a dock icon. Short enough that a
 * deliberate flick to the edge still feels immediate. */
#define DOCK_SLIDE_SECS 0.16
#define DOCK_HIDE_DELAY 0.45
#define DOCK_REVEAL_DELAY 0.18

/* Pointer travel (px) before a press on the bar becomes a real drag. Shared by
 * both gestures: repositioning the bar, and rearranging the icons. It is what
 * keeps a click from being a one-pixel drag, so a rearrange press that never
 * crosses it still launches the app (dock_icon_drag_end). */
#define DOCK_DRAG_THRESHOLD 6.0

/* Click feedback: a clicked icon dips in then springs back over this window so
 * the dock reacts to a launch/activate instead of sitting static. */
#define DOCK_CLICK_ANIM_SECS 0.22

static bool edge_is_vertical(syn_dock_edge_t e)
{
    return e == SYN_DOCK_EDGE_LEFT || e == SYN_DOCK_EDGE_RIGHT;
}

static double dock_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Press-pop scale for an icon at time `now`: 1.0 when idle, dipping to ~0.82
 * at the midpoint and springing back to 1.0 by the end (a single smooth
 * sine hump). Returns 1.0 once the animation window has elapsed. */
static double dock_click_scale(const syn_dock_entry_t *e, double now)
{
    if (e->anim_start <= 0.0) return 1.0;
    double t = (now - e->anim_start) / DOCK_CLICK_ANIM_SECS;
    if (t < 0.0 || t >= 1.0) return 1.0;
    return 1.0 - 0.18 * sin(t * M_PI);
}

static bool dock_entry_animating(const syn_dock_entry_t *e, double now)
{
    return e->anim_start > 0.0 &&
           (now - e->anim_start) < DOCK_CLICK_ANIM_SECS;
}

/* ── Entry model ─────────────────────────────────────────── */

void dock_rebuild(syn_server_t *s)
{
    syn_dock_entry_t merged[DOCK_MAX_ENTRIES];
    memset(merged, 0, sizeof(merged));
    int count = 0;

    /* Seed pinned entries in configured order. */
    for (int i = 0; i < s->config.dock_pin_count && count < DOCK_MAX_ENTRIES; i++) {
        syn_dock_entry_t *e = &merged[count++];
        snprintf(e->app_id, sizeof(e->app_id), "%s", s->config.dock_pin[i]);
        e->pinned = 1;
    }

    /* Merge in every mapped view across all workspaces: match an existing
     * (pinned) entry by app_id, or append a new running-only one. Views
     * with no app_id are skipped — nothing sane to key them by. */
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            if (!v->mapped) continue;
            const char *app_id = view_app_id(v);
            if (!app_id || !*app_id) continue;

            syn_dock_entry_t *e = NULL;
            for (int i = 0; i < count; i++)
                if (strcmp(merged[i].app_id, app_id) == 0) { e = &merged[i]; break; }
            if (!e) {
                if (count >= DOCK_MAX_ENTRIES) continue;
                e = &merged[count++];
                snprintf(e->app_id, sizeof(e->app_id), "%s", app_id);
            }
            e->running = 1;
            /* Prefer the focused instance as the click target when an
             * app_id has multiple mapped windows. */
            if (!e->primary_view || v == s->focused_view)
                e->primary_view = v;
        }
    }

    memcpy(s->dock_entries, merged, sizeof(merged));
    s->dock_entry_count = count;

    dock_relayout(s);
}

void dock_view_mapped(syn_view_t *v)
{
    dock_rebuild(v->server);
}

void dock_view_unmapped(syn_view_t *v)
{
    /* dock_rebuild() always recomputes from the live workspace lists rather
     * than patching entries in place, so there's no stale primary_view to
     * clean up here — just refresh. */
    dock_rebuild(v->server);
}

/* ── Geometry ────────────────────────────────────────────── */

/* rounded_rect() used to live here. It is cairo_rounded_rect() in
 * cairo_shapes.c now, because the right-click menus need the same four arcs to
 * round their own borders — one path rather than two that can drift. */

/* ── Magnification, headroom, and the clock cell ──────────
 *
 * THE DOCK HAS TWO RECTS NOW, and keeping them apart is what the rest of this
 * file is about.
 *
 *   the CANVAS  the buffer and the scene tree — what dock_geometry() returns,
 *               what gets cropped to the output and slid off the edge.
 *   the BODY    the painted slab, inset into the canvas by `head` pixels on the
 *               side facing away from the screen edge.
 *
 * They were the same rect until magnification. A swollen icon has to grow
 * somewhere, and growing it INTO a 64px bar means a ceiling of 64/48 = 1.33 —
 * not enough to read as magnification. So when `dock_magnify` is on the canvas
 * gains dock_headroom() of transparent room past the body, and icons grow out
 * through it, exactly the way macOS's do.
 *
 * The headroom is a CONSTANT for a given size and swell, not something that
 * grows with the effect as the pointer arrives: a canvas that resized under the
 * pointer would move the body it contains, and the body has to stay welded to
 * the screen edge. Only the icons move.
 *
 * The run axis is the other half. Scales are sampled from each icon's flat
 * position and the row is then laid out CUMULATIVELY, so neighbours slide apart
 * to open room rather than overlapping — the bar itself grows and re-centres,
 * which is the "slide over" half of the gesture. Sampling from the flat position
 * is deliberate: sampling from the magnified one feeds the layout its own output
 * and the row shivers.
 */
#define DOCK_MAG_SPAN      2.75   /* falloff reach, in flat cells either side */
#define DOCK_MAG_EASE_SECS 0.10   /* fade the effect in/out over this */

/*
 * Room for the swell — and it has to FOLLOW the swell, which is why this is a
 * function and `dock_magnify_scale` is a setting rather than the 1.60 literal
 * it used to be. The body is welded to the screen edge, so an icon with nowhere
 * to grow is not a smaller effect: it is an icon clipped off at the far side of
 * the canvas, silently, because nothing in the scene graph objects to a buffer
 * that is too small for what was drawn into it.
 *
 * Rounded up to a multiple of 8 the way the 32 this replaces was ("48 × 1.60 −
 * 48 = 29px, rounded up to a round number") — which means the stock desktop
 * still gets exactly 32 and this change is invisible until somebody moves the
 * row. Zero with magnify off, and that zero is what collapses the canvas back
 * onto the body.
 */
static int dock_headroom(const syn_config_t *c)
{
    if (!c->dock_magnify) return 0;
    double swell = dock_icon_size(c) * ((double)c->dock_magnify_scale - 1.0);
    if (swell < 0.0) swell = 0.0;
    return (int)(ceil(swell / 8.0) * 8.0);
}

/* The clock's own cell, and the apps button's beside it.
 *
 * The 92 is a FLOOR now rather than the width. It was the width, and that is
 * what put the rule between the icons and the clock hard against the first
 * digit the moment seconds and an am/pm were both on: "3:03:11 AM" at 17px is
 * as wide as the cell that was supposed to contain it. So the cell is measured
 * from the strings it will actually draw (dock_clock_run) and 92 is only what
 * it never goes below — a short "3:03" keeps the roomy cell it always had.
 *
 * Two numbers because the run axis is the long one: a horizontal bar can give
 * the clock as much width as it needs, a vertical column has only `dock_height`
 * to be wide in and pays on height instead. */
#define DOCK_CLOCK_RUN_H   92   /* floor for a horizontal bar's clock cell */
#define DOCK_CLOCK_RUN_V   40
#define DOCK_CLOCK_GUTTER  12   /* ink to cell edge, each side */
#define DOCK_CLOCK_LINE_GAP 3   /* between the time and the date, in a column */

/* Defined below, with the clock itself; the layout needs both up here. */
static void dock_clock_strings(syn_server_t *s, bool vertical,
                               char *time_s, size_t tn, char *date_s, size_t dn);
static long dock_clock_stamp(syn_server_t *s);

/* Everything on the clock scales with the slab, so a 200px dock does not draw a
 * 17px time adrift in the middle of it. At the stock 64 every one of these is
 * the literal it used to be — the multiplier is exactly 1 there. */
static int dock_clock_px(int base, int thick)
{
    return (int)lround(base * (thick / 64.0));
}

/* …with a floor, for the two numbers that are FONT SIZES. The thinnest dock is
 * 32px and would otherwise ask for an 8px date, which is a row of grey smudges
 * rather than a date. Offsets and gutters take dock_clock_px() raw: flooring a
 * 1px nudge at 6 would shove the time out of its own cell. */
static int dock_clock_font(int base, int thick)
{
    int v = dock_clock_px(base, thick);
    return v < 9 ? 9 : v;
}

/*
 * A 1×1 scratch surface, kept for the life of the process.
 *
 * The run length of the clock cell is GEOMETRY — the hit tests and the drag
 * need it exactly as much as the renderer does — and none of those callers has
 * a cairo context in hand. Measuring is the only honest way to size a cell
 * around text; the alternative is the constant that caused this.
 */
static cairo_t *dock_measure;

static cairo_t *dock_measure_cr(void)
{
    cairo_t *cr = dock_measure;
    if (!cr) {
        cairo_surface_t *surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cr = dock_measure = cairo_create(surf);
        cairo_surface_destroy(surf);   /* cr holds its own reference */
    }
    /* Re-selected every time rather than once at creation: the desktop's UI font
     * is a live setting (font.state), and a context that kept the face it was
     * born with would measure the cell in one font while dock_draw_clock() drew
     * it in another. Cheap — cairo caches the resolved face. */
    cairo_select_font_face(cr, syn_text_ui_font(), CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    return cr;
}

static double dock_text_w(cairo_t *cr, const char *text, double size)
{
    cairo_text_extents_t ext;
    cairo_set_font_size(cr, size);
    syn_text_extents(cr, text, &ext);
    return ext.width;
}

/*
 * ── The clock cell's size, and the two font sizes inside it ─────────────────
 *
 * ⚠ MEASURE AND DRAW HAVE TO AGREE, and this is the second time that has had to
 * be said in this file. dock_measure_cr() re-selects the UI font on every call
 * precisely so a cell is never measured in one face and drawn in another; the
 * font SIZES had exactly the same split, because dock_clock_run() measured at
 * 17/11 and dock_draw_clock() wrote 17/11 out again a few hundred lines away.
 *
 * That was harmless while the sizes were constants. It stopped being harmless
 * the moment the sizes had to move, which is what a VERTICAL column forces:
 *
 * ⛔ A COLUMN CANNOT WIDEN. A horizontal bar's clock cell grows along the run
 * until the string fits, which is what dock_clock_run() has always done. A
 * column's run is its HEIGHT — the width is `dock_height`, 64px at stock, and
 * nothing the clock does can change it. So the old code did the only thing left
 * and gave up: DOCK_CLOCK_RUN_V was a flat 40px cell with a 15px time drawn
 * centred in it. "12:34 PM" at 15px is about 65px wide. It ran straight off both
 * sides of the column and out of the dock — reported as the clock not fitting in
 * vertical mode, which is exactly what it was.
 *
 * The fix is the other axis: hold the width and bring the SIZE down until the
 * string fits it, then grow the run (the height) to hold both lines at whatever
 * size that turned out to be. Both answers come from here so the two callers
 * cannot drift again.
 */
typedef struct {
    int  run;            /* along the bar's long axis */
    int  t_px, d_px;     /* the time and date font sizes, as drawn */
    bool analog;         /* a face, not two lines of text */
} dock_clock_layout_t;

/* Largest size at or below `want` whose rendering of `text` fits `avail`, with
 * a floor: below about 9px a date is a row of grey smudges rather than a date,
 * and a clock nobody can read is not an improvement on one that overflows. */
static int dock_clock_fit(cairo_t *cr, const char *text, int want, int avail)
{
    for (int px = want; px > 9; px--)
        if (dock_text_w(cr, text, px) <= avail) return px;
    return 9;
}

static void dock_clock_layout(syn_server_t *s, bool vertical, int thick,
                              dock_clock_layout_t *out)
{
    memset(out, 0, sizeof(*out));

    /*
     * The analog face: a SQUARE cell, in both orientations, and that is the
     * whole reason it fixes the column. A dial has no wide side — it needs the
     * same number of pixels each way and a column has `thick` of them, so the
     * cell is thick × thick and the face fills it. Nothing is measured, nothing
     * can overflow, and the same cell reads identically on a bar and a column.
     */
    if (s->config.dock_clock_analog) {
        out->analog = true;
        out->run    = thick;
        return;
    }

    char time_s[32] = {0}, date_s[32] = {0};
    dock_clock_strings(s, vertical, time_s, sizeof time_s,
                       date_s, sizeof date_s);

    cairo_t *cr = dock_measure_cr();
    int gutter  = dock_clock_px(DOCK_CLOCK_GUTTER, thick);

    if (!vertical) {
        /* The bar's: the sizes are fixed and the CELL grows to fit them. */
        out->t_px = dock_clock_font(17, thick);
        out->d_px = dock_clock_font(11, thick);

        double w  = dock_text_w(cr, time_s, out->t_px);
        double dw = dock_text_w(cr, date_s, out->d_px);
        if (dw > w) w = dw;

        out->run = (int)lround(w) + 2 * gutter;
        int floor_run = dock_clock_px(DOCK_CLOCK_RUN_H, thick);
        if (out->run < floor_run) out->run = floor_run;
        return;
    }

    /* The column's: the CELL's width is fixed and the sizes come down to fit
     * it. A narrower gutter than the bar's, because the whole budget here is
     * `thick` — 12px each side of a 64px column is a third of it. */
    int avail = thick - 2 * (gutter / 2);
    if (avail < 16) avail = 16;

    out->t_px = dock_clock_fit(cr, time_s, dock_clock_font(15, thick), avail);
    out->d_px = dock_clock_fit(cr, date_s, dock_clock_font(10, thick), avail);
    /* The date never outsizes the time: it is the secondary line, and a fitted
     * time that came down to 10px beside a 10px date reads as two times. */
    if (out->d_px > out->t_px) out->d_px = out->t_px;

    /* Run = both lines, the gap between them, and a margin top and bottom. The
     * floor is what a short time on a stock dock has always had. */
    out->run = out->t_px + DOCK_CLOCK_LINE_GAP + out->d_px
             + 2 * dock_clock_px(6, thick);
    int floor_run = dock_clock_px(DOCK_CLOCK_RUN_V, thick);
    if (out->run < floor_run) out->run = floor_run;
}

/*
 * How much run the clock cell needs.
 *
 * Cached on the clock's own stamp (which carries the format and the analog
 * switch as well as the time — see dock_clock_stamp) and the thickness, because
 * dock_metrics() is on the pointer-motion path and runs per output per event.
 * The strings only change once a minute at stock.
 *
 * ⚠ THE ORIENTATION IS IN THE CACHE KEY. It was not, and it did not have to be
 * while the vertical answer was a constant that never reached this code; both
 * orientations are measured now, and one dock can be asked for both within a
 * frame — dock_slot_at() asks for the current edge while a drag is asking about
 * another.
 */
static int dock_clock_run(syn_server_t *s, bool vertical, int thick)
{
    static long cached_stamp = -1;
    static int  cached_thick = -1, cached_run = 0;
    static int  cached_vert = -1;
    long stamp = dock_clock_stamp(s);
    if (stamp == cached_stamp && thick == cached_thick &&
        cached_vert == (int)vertical)
        return cached_run;

    dock_clock_layout_t l;
    dock_clock_layout(s, vertical, thick, &l);

    cached_stamp = stamp; cached_thick = thick; cached_vert = (int)vertical;
    cached_run = l.run;
    return l.run;
}

/* Everything both the renderer and the hit tests need to agree about, derived
 * in one place from the config, the entry count and this output's magnification
 * state. Per OUTPUT, because only the screen the pointer is on magnifies — the
 * cell rects genuinely differ between mirrors now, which is why they cannot go
 * on the (server-global) entries any more. */
typedef struct {
    bool vertical;
    struct wlr_box ob;              /* the output box */
    int  x, y, w, h;                /* CANVAS rect, layout coords */
    int  thick;                     /* body thickness along the edge normal */
    int  head;                      /* transparent headroom past the body */
    int  bx, by, bw, bh;            /* BODY rect, canvas-local */
    int  n;                         /* cells laid out */
    int  icon, pad;                 /* flat icon size and the gap between */
    int  cx[DOCK_MAX_ENTRIES];      /* drawn cell origin, canvas-local */
    int  cy[DOCK_MAX_ENTRIES];
    int  cs[DOCK_MAX_ENTRIES];      /* drawn cell size (square) */
    int  clk_x, clk_y, clk_w, clk_h;   /* canvas-local; clk_w = 0 when off */
    int  clk_slot;                  /* icons to the clock's left; -1 when off */
    int  apps_x, apps_y, apps_s;    /* the show-all-apps cell; apps_s = 0 off */
    int  pwr_x, pwr_y, pwr_s;       /* the power-button cell; pwr_s = 0 off */
    int  base_run;                  /* the FLAT run length */
    int  base_origin;               /* layout coord where the flat run starts —
                                     * the origin o->dock.mag_run is measured
                                     * from, and it does not move when the
                                     * magnified bar grows */
} dock_metrics_t;

/*
 * Which gap along the run the clock sits in, resolved against the icons that
 * exist RIGHT NOW: 0 is before the first icon, `n` is past the last.
 *
 * The stored -1 means "past the last one", and it is a position rather than a
 * fallback. A clock pinned to slot 5 walks back up the row every time an app
 * quits, because the gap it was pinned to stops existing; "last" is the only
 * end of the row that survives apps coming and going, so it is what an
 * untouched clock keeps.
 *
 * A live drag overrides the stored value so the cell follows the cursor while
 * the button is down — the same way dock_display_order() previews an icon
 * rearrange.
 */
static int dock_clock_slot(syn_server_t *s, int n)
{
    int slot = s->config.dock_clock_slot;
    if (s->dock_drag.active && s->dock_drag.moved &&
        s->dock_drag.icon == DOCK_DRAG_CLOCK)
        slot = s->dock_drag.slot;
    if (slot < 0 || slot > n) slot = n;
    return slot;
}

/* The swell an icon whose FLAT centre is at `flat_c` currently has. A raised
 * cosine, not a linear ramp: the ramp has a corner at the pointer and the row
 * visibly kinks as it crosses an icon. */
static double dock_mag_scale(syn_output_t *o, double amount, double peak,
                             double flat_c, int icon, int pad)
{
    if (amount <= 0.0) return 1.0;
    double d = fabs(o->dock.mag_run - flat_c) / (DOCK_MAG_SPAN * (icon + pad));
    if (d >= 1.0) return 1.0;
    return 1.0 + (peak - 1.0) * amount * 0.5 * (1.0 + cos(M_PI * d));
}

/*
 * Where a cell sits on the CROSS axis. Each cell is anchored by the side FACING
 * the screen edge, so it swells away from it and the running dot underneath —
 * which is drawn off the flat cell, not the swollen one — stays put.
 *
 * `nudge` lifts a horizontal bar's icons off centre to leave room for that dot;
 * it scales with the icon, so a 200px dock does not put its dot through the
 * bottom of a 184px picture.
 */
static int dock_cell_cross(const dock_metrics_t *m, syn_dock_edge_t edge,
                           int icon, int size, int nudge)
{
    switch (edge) {
    case SYN_DOCK_EDGE_TOP:   return m->by + (m->thick - icon) / 2 - nudge;
    case SYN_DOCK_EDGE_LEFT:  return m->bx + (m->thick - icon) / 2;
    case SYN_DOCK_EDGE_RIGHT: return m->bx + (m->thick + icon) / 2 - size;
    case SYN_DOCK_EDGE_BOTTOM:
    default:                  return m->by + (m->thick + icon) / 2 - nudge - size;
    }
}

/* Fully-shown canvas rect, body rect and cell rects for this output's mirror on
 * the current edge. The "run" axis (length) grows with the entry count, the
 * clock cell, the apps button and the current magnification; the cross axis is
 * the fixed dock_height thickness plus any headroom. Shared by the renderer, the
 * hit tests and the auto-hide tick so all three agree on where the bar lives. */
static bool dock_metrics(syn_output_t *o, dock_metrics_t *m)
{
    syn_server_t *s = o->server;
    memset(m, 0, sizeof(*m));
    m->clk_slot = -1;

    output_box_of(s, o, &m->ob);
    if (m->ob.width <= 0 || m->ob.height <= 0) return false;

    syn_dock_edge_t edge = s->config.dock_edge;
    m->vertical = edge_is_vertical(edge);
    m->thick = s->config.dock_height;
    m->head  = dock_headroom(&s->config);

    int icon = dock_icon_size(&s->config), pad = dock_icon_pad(&s->config);
    m->icon = icon; m->pad = pad;
    int n = s->dock_entry_count;
    if (n > DOCK_MAX_ENTRIES) n = DOCK_MAX_ENTRIES;
    m->n = n;

    int clock_run = s->config.dock_clock
                    ? dock_clock_run(s, m->vertical, m->thick) : 0;
    int apps_run  = s->config.dock_apps_button ? icon : 0;
    /* Past the apps button, and that order is fixed: both are drawn at the end
     * of the run, and the destructive one is the one further from the icons. */
    int pwr_run   = s->config.dock_power_button ? icon : 0;
    if (clock_run > 0) m->clk_slot = dock_clock_slot(s, n);

    m->base_run = pad + n * (icon + pad)
                + (clock_run > 0 ? clock_run + pad : 0)
                + (apps_run  > 0 ? apps_run  + pad : 0)
                + (pwr_run   > 0 ? pwr_run   + pad : 0);
    if (n == 0 && clock_run == 0 && apps_run == 0 && pwr_run == 0)
        m->base_run = pad * 2;

    /* Flat centres first — dock_mag_scale() samples from where a cell WOULD be,
     * never from where it currently is. The clock's cell takes its place in this
     * walk like anything else, which is what lets it be dragged into the middle
     * of the row without the icons past it magnifying off the wrong centres. */
    double flat_c[DOCK_MAX_ENTRIES];
    double flat_apps_c, flat_pwr_c;
    {
        int run = pad;
        for (int i = 0; i <= n; i++) {
            if (i == m->clk_slot && clock_run > 0) run += clock_run + pad;
            if (i == n) break;
            flat_c[i] = run + icon / 2.0;
            run += icon + pad;
        }
        flat_apps_c = run + icon / 2.0;
        if (apps_run > 0) run += icon + pad;
        flat_pwr_c  = run + icon / 2.0;
    }

    /* Suppressed outright during a drag. The rearrange gesture measures cells
     * with flat arithmetic (dock_slot_at), and a row that resized under the
     * icon being dragged would move the very target the drop is aimed at. */
    double amount = (s->config.dock_magnify && !s->dock_drag.active)
                    ? o->dock.mag_amount : 0.0;
    double peak = s->config.dock_magnify_scale;

    int run = pad, clk_at = 0, apps_at = 0, apps_size = 0;
    int pwr_at = 0, pwr_size = 0;
    for (int i = 0; i <= n; i++) {
        if (i == m->clk_slot && clock_run > 0) {
            clk_at = run;
            run += clock_run + pad;
        }
        if (i == n) break;
        int size = (int)lround(icon * dock_mag_scale(o, amount, peak,
                                                     flat_c[i], icon, pad));
        m->cs[i] = size;
        if (m->vertical) m->cy[i] = run; else m->cx[i] = run;
        run += size + pad;
    }
    if (apps_run > 0) {
        apps_size = (int)lround(icon * dock_mag_scale(o, amount, peak,
                                                      flat_apps_c, icon, pad));
        apps_at = run;
        run += apps_size + pad;
    }
    if (pwr_run > 0) {
        pwr_size = (int)lround(icon * dock_mag_scale(o, amount, peak,
                                                     flat_pwr_c, icon, pad));
        pwr_at = run;
        run += pwr_size + pad;
    }
    int total_run = (n > 0 || clock_run > 0 || apps_run > 0 || pwr_run > 0)
                    ? run : pad * 2;

    int cross = m->thick + m->head;
    if (m->vertical) { m->w = cross;     m->h = total_run; }
    else             { m->w = total_run; m->h = cross;     }

    switch (edge) {
    case SYN_DOCK_EDGE_TOP:
        m->x = m->ob.x + (m->ob.width - m->w) / 2; m->y = m->ob.y;
        break;
    case SYN_DOCK_EDGE_LEFT:
        m->x = m->ob.x; m->y = m->ob.y + (m->ob.height - m->h) / 2;
        break;
    case SYN_DOCK_EDGE_RIGHT:
        m->x = m->ob.x + m->ob.width - m->w;
        m->y = m->ob.y + (m->ob.height - m->h) / 2;
        break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:
        m->x = m->ob.x + (m->ob.width - m->w) / 2;
        m->y = m->ob.y + m->ob.height - m->h;
        break;
    }
    if (m->x < m->ob.x) m->x = m->ob.x;   /* longer than the output: clip */
    if (m->y < m->ob.y) m->y = m->ob.y;

    /* The body, inset by the headroom on the side facing away from the edge. */
    if (m->vertical) {
        m->bw = m->thick; m->bh = m->h; m->by = 0;
        m->bx = (edge == SYN_DOCK_EDGE_RIGHT) ? m->head : 0;
    } else {
        m->bh = m->thick; m->bw = m->w; m->bx = 0;
        m->by = (edge == SYN_DOCK_EDGE_BOTTOM) ? m->head : 0;
    }

    int nudge = icon / 12;   /* 4px at the stock 48 */
    for (int i = 0; i < n; i++) {
        int c = dock_cell_cross(m, edge, icon, m->cs[i], nudge);
        if (m->vertical) m->cx[i] = c; else m->cy[i] = c;
    }
    if (apps_size > 0) {
        m->apps_s = apps_size;
        int c = dock_cell_cross(m, edge, icon, apps_size, nudge);
        if (m->vertical) { m->apps_x = c;       m->apps_y = apps_at; }
        else             { m->apps_x = apps_at; m->apps_y = c; }
    }
    if (pwr_size > 0) {
        m->pwr_s = pwr_size;
        int c = dock_cell_cross(m, edge, icon, pwr_size, nudge);
        if (m->vertical) { m->pwr_x = c;      m->pwr_y = pwr_at; }
        else             { m->pwr_x = pwr_at; m->pwr_y = c; }
    }

    if (clock_run > 0) {
        if (m->vertical) {
            m->clk_x = m->bx;    m->clk_y = clk_at;
            m->clk_w = m->thick; m->clk_h = clock_run;
        } else {
            m->clk_x = clk_at;    m->clk_y = m->by;
            m->clk_w = clock_run; m->clk_h = m->thick;
        }
    }

    /* Where the FLAT run starts, in layout coordinates. The magnified bar is
     * centred on the same point as the flat one, so this is fixed for a given
     * entry count — which is exactly what makes mag_run a stable input rather
     * than a value the layout it feeds keeps moving. */
    if (m->vertical) {
        m->base_origin = m->ob.y + (m->ob.height - m->base_run) / 2;
        if (m->base_origin < m->ob.y) m->base_origin = m->ob.y;
    } else {
        m->base_origin = m->ob.x + (m->ob.width - m->base_run) / 2;
        if (m->base_origin < m->ob.x) m->base_origin = m->ob.x;
    }
    return true;
}

/* The CANVAS rect, for the callers that only want to know where the tree goes
 * and how big its buffer is. */
static bool dock_geometry(syn_output_t *o, int *bx, int *by,
                          int *bar_w, int *bar_h)
{
    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return false;
    *bx = m.x; *by = m.y; *bar_w = m.w; *bar_h = m.h;
    return true;
}

/*
 * The bar's corner radius, clamped so it can never round past a capsule. Half
 * the SHORT side, so a horizontal bar is limited by its thickness and a vertical
 * column by its width.
 *
 * The row goes to 64 because a 200px dock can genuinely take it, and at the
 * default 64px thickness the same number has to mean 32.
 *
 * cairo_rounded_rect() applies the identical clamp for its own path (a bigger
 * radius turns the arcs inside out and draws a bow-tie), so this is NOT here for
 * the fill. It is here because two other things take the same number and neither
 * clamps: the specular hairline insets by it to stay out of the corner arcs, and
 * the blur node's fx_corner_radii is set from it — an unclamped radius there
 * leaves the frosted patch a different shape from the pane on top of it, which
 * shows as a bright rim in each corner.
 *
 * Measured on the BODY, never the canvas: the canvas is taller than the slab by
 * the magnification headroom, and feeding that in would let the radius exceed
 * half the slab and bow-tie the very path this clamp exists to protect.
 */
static double dock_bar_radius(syn_server_t *s, int bar_w, int bar_h)
{
    double r = s->config.dock_radius;
    if (r < 0.0) r = 0.0;
    double cap = (bar_w < bar_h ? bar_w : bar_h) / 2.0;
    return r > cap ? cap : r;
}

/* Which flat cell a point on the run axis falls in, clamped to the icons that
 * exist. `run` is canvas-local along the bar's long axis. Flat arithmetic on
 * purpose — its one caller is the rearrange drag, and dock_metrics() suppresses
 * magnification for the length of that gesture.
 *
 * ⚠ THE CLOCK'S CELL IS IN THE RUN TOO, and it is not an icon. Magnification is
 * suppressed during the drag; the clock is not, so every icon past it sits one
 * cell of a different width further along than icon arithmetic alone would put
 * it. Without the subtraction below, dragging an icon across a clock parked in
 * the middle of the row drops it a slot early — silently, and only on the
 * desktops that have moved their clock. */
static int dock_slot_at(syn_server_t *s, int run, int count)
{
    if (count <= 0) return 0;
    int icon = dock_icon_size(&s->config), pad = dock_icon_pad(&s->config);

    if (s->config.dock_clock) {
        int clk_slot = dock_clock_slot(s, count);
        int clk_run  = dock_clock_run(s, edge_is_vertical(s->config.dock_edge),
                                      s->config.dock_height) + pad;
        if (run >= pad + clk_slot * (icon + pad)) run -= clk_run;
    }

    int cell = icon + pad;
    int i = (run - pad) / cell;
    if (run < pad) i = 0;                /* integer division truncates toward 0 */
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    return i;
}

/*
 * Which GAP the clock wants, given a run coordinate along the body.
 *
 * Counted off the cells as they are currently DRAWN rather than off flat
 * arithmetic, and that is what keeps the gesture from oscillating: inserting
 * the clock at slot k pushes every icon past it along by the cell's width, so
 * the icon that just decided the answer moves AWAY from the cursor. The
 * hysteresis is free and it is in the right direction — one more nudge is
 * needed to come back than was needed to go.
 */
static int dock_clock_slot_at(const dock_metrics_t *m, double run)
{
    int slot = 0;
    for (int i = 0; i < m->n; i++) {
        double c = (m->vertical ? m->cy[i] : m->cx[i]) + m->cs[i] / 2.0;
        if (run > c) slot = i + 1;
    }
    return slot;
}

/*
 * The order the icons are DRAWN in this frame.
 *
 * Identity, except while an icon is being dragged: then the dragged entry is
 * lifted out of the run and re-inserted at the slot it currently hovers, so the
 * others shuffle to open a gap under it. That shuffle is the whole feedback of
 * the gesture — without it a dragged icon is a picture sliding over a bar that
 * has not agreed to anything, and you find out where it landed on release.
 *
 * Fills `order` with entry indices and returns the count. The dragged entry is
 * still IN the order (at its target slot); the renderer skips its cell and
 * paints it under the cursor instead, so the gap is exactly icon-sized.
 */
static int dock_display_order(syn_server_t *s, int *order, int max)
{
    int n = s->dock_entry_count;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) order[i] = i;

    int from = s->dock_drag.icon;
    if (!s->dock_drag.active || !s->dock_drag.moved || from < 0 || from >= n)
        return n;

    int to = s->dock_drag.slot;
    if (to < 0) to = 0;
    if (to >= n) to = n - 1;
    if (to == from) return n;

    /* Shift the run between the two positions by one and drop it in. */
    if (to < from)
        for (int i = from; i > to; i--) order[i] = order[i - 1];
    else
        for (int i = from; i < to; i++) order[i] = order[i + 1];
    order[to] = from;
    return n;
}

/* True while this output shows a mapped, non-minimized fullscreen window on the
 * active workspace. Mirrors layer_update_occlusion's rule (layer.c) — the dock
 * must yield to a fullscreen game/video the same way the top-layer bar does. */
static bool dock_output_fullscreen(syn_output_t *o)
{
    syn_server_t *s = o->server;
    syn_view_t *v;
    wl_list_for_each(v, &server_active_workspace(s)->windows, link) {
        if (v->output != o) continue;
        if (v->mapped && v->fullscreen && !v->minimized) return true;
    }
    return false;
}

/* Show only the sub-rectangle of the dock buffer that lands on `o`, or the
 * whole thing when w/h match the buffer.
 *
 * The dock hides by sliding its node past its own output's edge. That is
 * invisible on a single monitor, but the tree hangs off the scene ROOT in
 * layout coordinates, so on a stacked/side-by-side layout "off my bottom edge"
 * is "on top of my neighbour" — the top monitor's dock slid down and painted
 * itself across the screen below for the whole animation. Nothing clipped it,
 * because a wlr_scene_tree has no clip; the crop has to go on the buffer.
 *
 * The crop goes on the buffer CHILD, not on the tree: dock_entry_at() reads
 * icon hit-boxes relative to o->dock.tree->node, so the tree has to stay
 * pinned to the dock canvas's origin even when only part of it is painted. */
static void dock_set_crop(syn_output_t *o, int sx, int sy, int w, int h,
                          int buf_w, int buf_h)
{
    if (!o->dock.icons_buf) return;
    if (w == buf_w && h == buf_h) {
        wlr_scene_node_set_position(&o->dock.icons_buf->node, 0, 0);
        wlr_scene_buffer_set_source_box(o->dock.icons_buf, NULL);
        wlr_scene_buffer_set_dest_size(o->dock.icons_buf, buf_w, buf_h);
        return;
    }
    wlr_scene_node_set_position(&o->dock.icons_buf->node, sx, sy);
    struct wlr_fbox src = { .x = sx, .y = sy, .width = w, .height = h };
    wlr_scene_buffer_set_source_box(o->dock.icons_buf, &src);
    wlr_scene_buffer_set_dest_size(o->dock.icons_buf, w, h);
}

/* Place the tree at its slide offset and enable it only while any part is
 * on-screen. slide_progress 1 = flush against the edge, 0 = pushed fully off
 * it (along the edge normal). While this output's dock is being dragged, the
 * bar floats under the cursor instead. Called after (re)rendering and every
 * anim tick. */
/*
 * Does the dock float over windows on this output, or do they cover it?
 *
 * Three answers folded into one, in the order they override each other:
 *
 *   a FULLSCREEN window always wins. The dock's tree is a UI sibling of
 *   window_tree, so "raised" floats it over everything — including a fullscreen
 *   game or video, which only raises within window_tree.
 *
 *   an AUTO-HIDING dock is always on top. It is summoned by the pointer, over
 *   whatever happens to be there; arriving behind that window would mean
 *   revealing nothing, which is not a mode anyone would choose.
 *
 *   otherwise `dock_on_top` decides, and it is OFF by default. An always-visible
 *   dock that floats is a strip of screen a maximized window can never be in
 *   front of; tucked below window_tree it is furniture on the desktop, still
 *   above the wallpaper and the bottom layer.
 */
static bool dock_floats_over_windows(syn_server_t *s)
{
    return s->config.dock_autohide || s->config.dock_on_top;
}

static bool dock_on_top_here(syn_output_t *o)
{
    return dock_floats_over_windows(o->server) && !dock_output_fullscreen(o);
}

/* Place the tree at its slide offset and enable it only while any part is
 * on-screen. slide_progress 1 = flush against the edge, 0 = pushed fully off
 * it (along the edge normal). While this output's dock is being dragged, the
 * bar floats under the cursor instead. Called after (re)rendering and every
 * anim tick. */
static void dock_apply_position(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    dock_metrics_t m;
    bool have = dock_metrics(o, &m);

    /* Dragging THE BAR: float freely under the cursor, always visible.
     * Deliberately uncropped — a drag is how the dock is moved between edges and
     * outputs, so it has to be able to cross them.
     *
     * Neither CELL gesture is this. The bar stays exactly where it is: floating
     * it would carry the row the gesture is rearranging along with the cursor,
     * and there would be nothing for the dragged cell to move relative to.
     *
     * ⚠ DOCK_DRAG_BAR by name, never `icon < 0`. That test read as "not an
     * icon" for as long as there were only two gestures, and DOCK_DRAG_CLOCK is
     * -2: the clock drag took this branch and flung the whole dock to
     * `float_x/float_y`, which a clock drag never writes — 0,0 on a fresh
     * session. The cell could not be placed after that either, because
     * dock_clock_drag_motion() turns the cursor into a run coordinate by
     * subtracting this very node position. That is the whole of why the clock
     * "could not be moved" while every model test passed: the tests stub
     * create_cairo_buf() to NULL, dock_render_output() returns before it reaches
     * dock_apply_position(), and this line is in the half they do not run. */
    if (s->dock_drag.active && s->dock_drag.moved &&
        s->dock_drag.icon == DOCK_DRAG_BAR &&
        s->dock_drag.output == o) {
        if (have) dock_set_crop(o, 0, 0, m.w, m.h, m.w, m.h);
        wlr_scene_node_set_position(&o->dock.tree->node,
                                    (int)s->dock_drag.float_x,
                                    (int)s->dock_drag.float_y);
        wlr_scene_node_set_enabled(&o->dock.tree->node, true);
        wlr_scene_node_raise_to_top(&o->dock.tree->node);
        return;
    }

    if (!s->config.dock_enabled || !have) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    double p = o->dock.slide_progress;
    double off = 1.0 - p;
    /* Travel is the BODY's thickness, not the canvas's. The magnification
     * headroom is transparent, so sliding by it would spend part of the
     * animation moving nothing anyone can see — and, at a fixed duration, make
     * the visible part of the slide faster on a magnifying dock than on a flat
     * one. */
    int travel = m.thick;
    int x = m.x, y = m.y;
    switch (s->config.dock_edge) {
    case SYN_DOCK_EDGE_TOP:    y = m.y - (int)lround(off * travel); break;
    case SYN_DOCK_EDGE_LEFT:   x = m.x - (int)lround(off * travel); break;
    case SYN_DOCK_EDGE_RIGHT:  x = m.x + (int)lround(off * travel); break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:                   y = m.y + (int)lround(off * travel); break;
    }

    /* Clip the slid rect to this output and paint only that part. */
    int cx0 = x > m.ob.x ? x : m.ob.x;
    int cy0 = y > m.ob.y ? y : m.ob.y;
    int cx1 = (x + m.w) < (m.ob.x + m.ob.width)  ? (x + m.w) : (m.ob.x + m.ob.width);
    int cy1 = (y + m.h) < (m.ob.y + m.ob.height) ? (y + m.h) : (m.ob.y + m.ob.height);
    int cw = cx1 - cx0, ch = cy1 - cy0;

    bool visible = p > 0.001 && cw > 0 && ch > 0;
    if (visible) {
        dock_set_crop(o, cx0 - x, cy0 - y, cw, ch, m.w, m.h);
        /* The crop just changed the buffer's painted size and offset, and the
         * blur companion is sized and placed FROM those — so it has to be
         * re-synced here and not only after a render, or the frosted patch keeps
         * the pose it had before the slide and hangs off the edge for the whole
         * animation. Same class of staleness as the window blur that kept its
         * old size across a resize; see blur_sync_geometry() in anim.c. */
        syn_buffer_backdrop_blur(o->dock.icons_buf,
                                 dock_style_is_glass(&s->config) && s->config.blur,
                                 (int)lround(dock_bar_radius(s, m.bw, m.bh)));
    }
    wlr_scene_node_set_position(&o->dock.tree->node, x, y);
    wlr_scene_node_set_enabled(&o->dock.tree->node, visible);
    if (visible) {
        if (dock_on_top_here(o))
            wlr_scene_node_raise_to_top(&o->dock.tree->node);
        else
            wlr_scene_node_place_below(&o->dock.tree->node, &s->window_tree->node);
    }
}

/* ── Rendering ───────────────────────────────────────────── */

/*
 * The bar's body.
 *
 * SOLID is what the dock has always drawn: one flat fill of the theme's panel
 * surface with the theme's accent stroked round it.
 *
 * GLASS is the macOS 26 treatment, and it is three things rather than "the same
 * thing at a lower alpha" — that was the first attempt and it reads as a faded
 * dock, not a frosted one:
 *
 *   1. A real BACKDROP BLUR behind the buffer (wired up by the caller). This is
 *      the one that does the work. Frosted glass is defined by what it does to
 *      what is behind it, and no amount of alpha is a substitute.
 *   2. A gradient that is MORE transparent at the lit edge than the far one, so
 *      the surface has a direction. A flat alpha over a blur looks like a sheet
 *      of plastic; the falloff is what makes it read as a thick pane.
 *   3. A specular hairline just inside the lit edge, and a rim instead of the
 *      accent stroke. The accent is right for a panel that is part of the shell
 *      furniture and wrong for one that is pretending to be a piece of glass
 *      lying on the wallpaper — it outlines the shape and kills the illusion.
 *
 * Which way is "lit" is the edge the dock lives on: light comes from away from
 * that edge, so a bottom dock is lit along its top and a left column along its
 * right. Getting this wrong is not subtle — a bottom dock lit from underneath
 * looks like it is glowing.
 *
 * The pale/dark split is the same one contrast.c draws everywhere else. A white
 * rim is invisible on Tahoe's near-white surface and a black one is invisible on
 * SYNAPSE's navy, so each takes the one that shows.
 */
static void dock_paint_body(syn_server_t *s, cairo_t *cr,
                            int bar_w, int bar_h, double radius, bool glass)
{
    const float *pb = s->config.panel_bg;
    /* config.c has already clamped this to 0.00-1.00. It is NOT re-floored here:
     * the 0.05 that used to sit on this line was a second, quieter guard against
     * the same "invisible dock" the 0.20 in config.c was aimed at, and between
     * them a row set to 0.00 drew a body you could still see. The icons are
     * painted over this at full opacity, so a zero body is a row of icons on the
     * wallpaper — see the note on dock_opacity in config.c. */
    double a = s->config.dock_opacity;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    /*
     * How present the dock's CHROME is — the outline, the rim, the specular.
     *
     * ⚠ A CLEAR DOCK WITH AN OUTLINE IS NOT A CLEAR DOCK. Those three strokes
     * carry their own literal alphas, so dropping the body to 0.00 left the
     * shape drawn in full: a rectangle of nothing with a bright edge round it,
     * which reads as a rendering fault rather than as glass.
     *
     * A ramp and not a cutoff, and it bites only at the bottom of the range:
     * 1.0 everywhere above 0.35, so every dock anyone has ever configured looks
     * exactly as it did, and falling to 0 with the body under it. The rule it
     * states is the one that was missing — the chrome of a surface cannot be
     * more present than the surface.
     */
    double chrome_a = a < 0.35 ? a / 0.35 : 1.0;

    cairo_rounded_rect(cr, 0, 0, bar_w, bar_h, radius);

    if (!glass) {
        /* Body: the theme's panel surface, the same one render.c fills every
         * other compositor-drawn panel with. This was a literal 0.06/0.06/0.12 —
         * frozen here back when only the ACCENT was theme data, so the dock kept
         * SYNAPSE's near-black navy under a Gruvbox or XP desktop exactly as the
         * panels did before panel_bg existed. Stock is unaffected: SYNAPSE's
         * panel_bg IS 0.06/0.06/0.12 (theme.c), which is why the literal went
         * unnoticed.
         *
         * The alpha does NOT come from panel_bg[3] — the dock floats over the
         * wallpaper and wants to be translucent, while the panels it borrows the
         * colour from are opaque surfaces. It was a literal 0.80 for the same
         * reason the radius was a literal 16, and it is `dock_opacity` now. */
        cairo_set_source_rgba(cr, pb[0], pb[1], pb[2], a);
        cairo_fill_preserve(cr);
        /* Themed outline. This used to be a literal 0.00/0.85/0.75 — which is
         * the DEFAULT panel accent, frozen here before the accent became theme
         * data. So the dock kept SYNAPSE's house cyan on a Gruvbox or win95
         * desktop and was the one piece of chrome that never joined in.
         * panel_accent is the single colour every other panel already uses for
         * its rules and headers.
         *
         * Slightly heavier than the old 1px at 0.35 alpha: an outline that is
         * meant to tie the dock to the rest of the desktop has to actually be
         * visible against a wallpaper. */
        cairo_set_source_rgba(cr, s->config.panel_accent[0],
                              s->config.panel_accent[1],
                              s->config.panel_accent[2], 0.55 * chrome_a);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        return;
    }

    bool pale = syn_rel_luminance(pb[0], pb[1], pb[2]) > SURFACE_PALE;
    syn_dock_edge_t edge = s->config.dock_edge;

    /* From the lit edge to the far one, in canvas coordinates. */
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    switch (edge) {
    case SYN_DOCK_EDGE_TOP:    y0 = bar_h; y1 = 0;     break;  /* lit from below */
    case SYN_DOCK_EDGE_LEFT:   x0 = bar_w; x1 = 0;     break;
    case SYN_DOCK_EDGE_RIGHT:  x0 = 0;     x1 = bar_w; break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:                   y0 = 0;     y1 = bar_h; break;
    }

    cairo_pattern_t *pat = cairo_pattern_create_linear(x0, y0, x1, y1);
    /* Thinnest at the lit edge, where the blur shows through most. */
    cairo_pattern_add_color_stop_rgba(pat, 0.0, pb[0], pb[1], pb[2], a * 0.82);
    cairo_pattern_add_color_stop_rgba(pat, 0.5, pb[0], pb[1], pb[2], a);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, pb[0], pb[1], pb[2], a * 1.06 > 1.0
                                                                     ? 1.0 : a * 1.06);
    cairo_set_source(cr, pat);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pat);

    /* The rim. Whichever of black/white shows on this theme's surface, kept
     * faint: it is there to give the pane an edge, not to outline the dock. */
    if (pale) cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.13 * chrome_a);
    else      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.22 * chrome_a);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* The specular. One hairline just inside the lit edge, along the run axis
     * and stopping short of the corners — carried into the arcs it would just be
     * a second rim, and the point of a highlight is that it is where the light
     * hits and nowhere else. Always white: a specular is the light source, not
     * the surface, so it does not follow the pale/dark split above. */
    double inset = 1.5, r = radius;
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, (pale ? 0.75 : 0.38) * chrome_a);
    switch (edge) {
    case SYN_DOCK_EDGE_TOP:
        cairo_move_to(cr, r, bar_h - inset);
        cairo_line_to(cr, bar_w - r, bar_h - inset);
        break;
    case SYN_DOCK_EDGE_LEFT:
        cairo_move_to(cr, bar_w - inset, r);
        cairo_line_to(cr, bar_w - inset, bar_h - r);
        break;
    case SYN_DOCK_EDGE_RIGHT:
        cairo_move_to(cr, inset, r);
        cairo_line_to(cr, inset, bar_h - r);
        break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:
        cairo_move_to(cr, r, inset);
        cairo_line_to(cr, bar_w - r, inset);
        break;
    }
    cairo_stroke(cr);
}

/* One icon, at (ix,iy) in the dock canvas, scaled about its own centre. Pulled
 * out of the render loop because the dragged icon is drawn by the same code at a
 * different place and a different scale — two copies would be two chances for a
 * lifted icon to stop looking like the one it was lifted from. */
/*
 * ── The icon, already the size the dock draws it ────────────────────────────
 *
 * icon_lookup() hands back the icon at the size it was decoded from disk, which
 * for a hicolor PNG is 128x128 and for some apps 512x512. The dock draws it at
 * 48. Painting it therefore meant cairo resampling the source down to the cell
 * on EVERY draw, with CAIRO_FILTER_GOOD, once per icon.
 *
 * That is affordable when the dock repaints because something changed. It is
 * not affordable while an icon is being DRAGGED: dock_icon_drag_motion() calls
 * dock_relayout() for every pixel the icon travels, and dock_relayout() repaints
 * the dock on EVERY output. So one pixel of drag on a three-monitor desk was
 * three full dock canvases, each resampling every pinned icon from 128x128.
 * Reported as dragging a dock icon being laggy, which is exactly what it was.
 *
 * So the scaled copy is made once and kept. A drag is then N cheap 1:1 blits.
 *
 * ⚠ IT HAS TO BE DROPPED WHEN THE ICONS CHANGE, and the accent retint changes
 * them without changing anything the dock can see — icon_lookup() returns the
 * same entry with a different picture inside it. icon_generation() is what says
 * so. Without this the dock would keep the pre-accent icons until something
 * else happened to evict them, which on a desktop that retints from the
 * wallpaper is a visible and very confusing lag.
 */
typedef struct {
    char             app_id[128];
    cairo_surface_t *surf;          /* dock_icon_cache_size square, or NULL */
} dock_icon_cache_t;

static dock_icon_cache_t dock_icon_cache[DOCK_MAX_ENTRIES * 2];
static int      dock_icon_cache_n   = 0;
static unsigned dock_icon_cache_gen = 0;
/* ⚠ AND THE SIZE, now that the Dock size row changes it. The cache holds each
 * icon rasterized at exactly the cell size; keeping a 48px surface after the
 * dock grew to 184 would leave every picture on the bar upscaled and soft, and
 * nothing would say so — the layout is right, the pixels are not. */
static int      dock_icon_cache_size = 0;

static void dock_icon_cache_flush(void)
{
    for (int i = 0; i < dock_icon_cache_n; i++)
        if (dock_icon_cache[i].surf)
            cairo_surface_destroy(dock_icon_cache[i].surf);
    dock_icon_cache_n = 0;
}

/* The icon at exactly `cell` px square, or NULL if this app has no picture (the
 * caller draws a monogram instead). */
static cairo_surface_t *dock_icon_at_cell(const char *app_id, int cell)
{
    unsigned gen = icon_generation();
    if (gen != dock_icon_cache_gen || cell != dock_icon_cache_size) {
        dock_icon_cache_flush();
        dock_icon_cache_gen  = gen;
        dock_icon_cache_size = cell;
    }
    for (int i = 0; i < dock_icon_cache_n; i++)
        if (strcmp(dock_icon_cache[i].app_id, app_id) == 0)
            return dock_icon_cache[i].surf;

    const syn_icon_entry_t *ic = icon_lookup(app_id);
    cairo_surface_t *scaled = NULL;
    if (ic->icon_surface) {
        double sw = cairo_image_surface_get_width(ic->icon_surface);
        double sh = cairo_image_surface_get_height(ic->icon_surface);
        if (sw > 0 && sh > 0) {
            scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                cell, cell);
            cairo_t *sc = cairo_create(scaled);
            cairo_scale(sc, cell / sw, cell / sh);
            cairo_set_source_surface(sc, ic->icon_surface, 0, 0);
            /* The one place the expensive filter is still paid — once per icon
             * per generation, instead of once per icon per repaint. */
            cairo_pattern_set_filter(cairo_get_source(sc), CAIRO_FILTER_GOOD);
            cairo_paint(sc);
            cairo_destroy(sc);
        }
    }
    /* A full table is not an error: fall back to painting from the source, which
     * is what this did everywhere before. */
    if (dock_icon_cache_n < (int)(sizeof dock_icon_cache / sizeof *dock_icon_cache)) {
        dock_icon_cache_t *e = &dock_icon_cache[dock_icon_cache_n++];
        snprintf(e->app_id, sizeof e->app_id, "%s", app_id);
        e->surf = scaled;
        return e->surf;
    }
    if (scaled) cairo_surface_destroy(scaled);
    return NULL;
}

/* `base` is the FLAT cell size — what the cache rasterizes at — and `icon` is
 * how big this particular cell is drawn right now, which magnification makes
 * bigger. Passing both is what keeps one swollen icon from evicting the cache
 * for every other one on the bar. */
static void dock_draw_icon(cairo_t *cr, const char *app_id,
                           double ix, double iy, int icon, int base,
                           double scale)
{
    cairo_save(cr);
    if (scale != 1.0) {
        double cx = ix + icon / 2.0, cy = iy + icon / 2.0;
        cairo_translate(cr, cx, cy);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, -cx, -cy);
    }

    cairo_surface_t *cell = dock_icon_at_cell(app_id, base);
    if (cell) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        /* 1:1 for a flat cell; a magnified one is the cached picture scaled up,
         * which is what the headroom exists to make room for. */
        if (icon != base)
            cairo_scale(cr, (double)icon / base, (double)icon / base);
        cairo_set_source_surface(cr, cell, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        icon_draw_monogram(cr, app_id, ix, iy, icon);
    }
    cairo_restore(cr);
}

/*
 * The GNOME-style "show all apps" button — the one that opens the FULL-SCREEN
 * application page (appgrid.c), not the bar's start menu. It briefly did the
 * latter, and the difference is the whole feature: a dock of pinned icons has
 * no route to an application that is not on it, and a menu that needs the bar
 * running is not a route on a desktop whose bar is off.
 *
 * A 3×3 grid of dots drawn in the panel ink, not an icon out of the theme:
 * there is no .desktop behind this button, every icon theme that has a grid
 * glyph draws it at a different weight, and the ink is by definition the colour
 * that reads on this bar — the same rule the running dot follows, and for the
 * same reason (a white glyph vanishes on XP's beige).
 *
 * Everything is a fraction of the cell so it scales with the Dock size row
 * along with the icons beside it.
 */
/*
 * The IEC 5009 power mark: a ring broken at the top with a stroke through the
 * break. Drawn rather than pulled from the icon theme for the reason the grid
 * of dots beside it is — the cell has to be the panel's ink at the panel's
 * alpha on every one of the fourteen themes, and a theme icon is a fixed
 * picture in somebody else's colour.
 *
 * Every measurement is a fraction of the cell, so it swells with the rest of the
 * row under magnification instead of sitting at 48px inside a 120px cell.
 */
static void dock_draw_power(syn_server_t *s, cairo_t *cr,
                            double x, double y, int size)
{
    double cx = x + size / 2.0, cy = y + size / 2.0;
    double r  = size * 0.28;
    double lw = size * 0.094;   /* 4.5px in a 48px cell */

    cairo_save(cr);
    cairo_set_source_rgba(cr, s->config.panel_ink[0], s->config.panel_ink[1],
                          s->config.panel_ink[2], 0.92);
    cairo_set_line_width(cr, lw);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* The ring, from just past top-left round to just short of top-right. Cairo
     * angles run clockwise from 3 o'clock, so the gap is centred on -M_PI_2. */
    double gap = 0.38;   /* radians each side of straight up */
    cairo_arc(cr, cx, cy, r, -M_PI_2 + gap, -M_PI_2 - gap + 2 * M_PI);
    cairo_stroke(cr);

    /* The stroke through the gap. It starts ABOVE the ring and ends inside it,
     * which is what the standard draws and what keeps the mark readable once
     * the whole thing is 14px on a small dock. */
    cairo_move_to(cr, cx, cy - r - lw * 0.55);
    cairo_line_to(cr, cx, cy - r * 0.30);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void dock_draw_apps(syn_server_t *s, cairo_t *cr,
                           double x, double y, int size)
{
    double r    = size * 0.072;    /* 3.5px in a 48px cell */
    double step = size * 0.26;
    double cx0  = x + size / 2.0 - step;
    double cy0  = y + size / 2.0 - step;

    cairo_set_source_rgba(cr, s->config.panel_ink[0], s->config.panel_ink[1],
                          s->config.panel_ink[2], 0.92);
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            cairo_arc(cr, cx0 + col * step, cy0 + row * step, r, 0, 2 * M_PI);
            cairo_fill(cr);
        }
}

/* ── The clock ───────────────────────────────────────────
 *
 * Off by default, and the reason is worth stating: the bar already has a clock,
 * and a desktop that shows the time twice is a choice somebody makes rather than
 * one they should have to undo. On, it takes a cell of its own INSIDE the bar —
 * not floating beside it — and the run grows to hold it.
 *
 * Where that cell sits is the user's (dock_clock_slot): it starts past the last
 * icon and can be dragged into any gap in the row, which is why the layout walk
 * in dock_metrics() steps through slots rather than appending the clock at the
 * end. Nothing else about it changes with the position.
 *
 * It reads its 12/24-hour and seconds settings out of the SAME syn_clock_t the
 * Clock & Time panel writes and the bar's clock module follows (clock.state), so
 * there is one answer to "does this desktop use a 24-hour clock" and the dock is
 * not a second place to set it.
 */
static void dock_clock_strings(syn_server_t *s, bool vertical,
                               char *time_s, size_t tn,
                               char *date_s, size_t dn)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    const char *tfmt;
    if (s->clock.fmt24) tfmt = s->clock.seconds ? "%H:%M:%S" : "%H:%M";
    else                tfmt = s->clock.seconds ? "%l:%M:%S" : "%l:%M";
    strftime(time_s, tn, tfmt, &tm);
    /* %l pads with a space; the cell is centred, so a leading blank would put a
     * one-o'clock time visibly off-centre against a ten-o'clock one. */
    while (*time_s == ' ') memmove(time_s, time_s + 1, strlen(time_s));

    if (!s->clock.fmt24) {
        char ap[8];
        strftime(ap, sizeof(ap), "%p", &tm);
        size_t len = strlen(time_s);
        if (len + 1 + strlen(ap) < tn)
            snprintf(time_s + len, tn - len, " %s", ap);
    }

    /* A column has `dock_height` to be wide in — 64px at stock — so it gets the
     * short form. A horizontal bar's cell is measured to fit and can take the
     * weekday. */
    strftime(date_s, dn, vertical ? "%d %b" : "%a %d %b", &tm);
}

/* The value the clock last DREW, so a 1 Hz wake only repaints when the string it
 * would produce has actually changed. Seconds off, that is once a minute.
 *
 * ⚠ THE FORMAT IS IN THE STAMP, not just the time, and it has to be. clock.state
 * is loaded by clock_init() — which runs AFTER dock_init() — and the Clock &
 * Time panel can change 12/24-hour at any moment after that. Both change the
 * string while leaving the minute alone, so a stamp that counted only minutes
 * would leave a dock that had already drawn showing the old format until the
 * clock happened to tick over. */
static long dock_clock_stamp(syn_server_t *s)
{
    time_t now = time(NULL);
    /* ⚠ ANALOG TICKS AT 1 Hz WHATEVER THE SECONDS SETTING SAYS — but only when
     * there is a second hand to move, which is the same setting. A face with no
     * second hand is as still as the digits it replaced. */
    long t = (long)(s->clock.seconds ? now : now / 60);
    return t * 8 + (s->config.dock_clock_analog ? 4 : 0)
                 + (s->clock.fmt24 ? 2 : 0) + (s->clock.seconds ? 1 : 0);
}

/* Centre `text` at `size` px in the box, `dy` down from its middle. */
static void dock_clock_line(cairo_t *cr, const char *text, double size,
                            double cx, double cy)
{
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    syn_text_extents(cr, text, &ext);
    cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, cy);
    syn_show_text(cr, text);
}

/*
 * The analog face.
 *
 * ⚠ THIS IS WHY IT EXISTS: it is the only clock that fits a vertical dock
 * without compromise. A column is `dock_height` wide and a time is a WIDE
 * string; a dial is square, so the same 64px that cannot hold "12:34 PM" holds
 * a complete clock with room to spare. On a horizontal bar it is a preference —
 * on a column it is the answer.
 *
 * Drawn rather than themed, like the apps and power buttons beside it: the ink
 * and the accent are the panel's, on all fourteen themes.
 *
 * `frac` on the hour and minute hands is not decoration. A minute hand that
 * jumps and an hour hand that sits exactly on 3 until it snaps to 4 is a clock
 * that reads wrong twice an hour — the hour hand belongs between the numerals
 * for 58 of every 60 minutes, and that is most of what makes a dial legible at
 * a glance.
 */
static void dock_draw_clock_face(syn_server_t *s, cairo_t *cr,
                                 const dock_metrics_t *m)
{
    double cx = m->clk_x + m->clk_w / 2.0;
    double cy = m->clk_y + m->clk_h / 2.0;
    double box = (m->clk_w < m->clk_h ? m->clk_w : m->clk_h);
    double r   = box / 2.0 - box * 0.10;
    if (r < 4) return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    const float *ink = s->config.panel_ink;
    const float *acc = s->config.panel_accent;

    /* The dial: a rim, and a tick at each hour with the quarters longer. Four
     * long marks is what tells you which way up a face with no numerals is, and
     * at this size numerals would be three pixels tall. */
    cairo_save(cr);
    cairo_set_line_width(cr, r * 0.075);
    cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.30);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_stroke(cr);

    for (int i = 0; i < 12; i++) {
        double a = i * M_PI / 6.0 - M_PI_2;
        bool quarter = (i % 3) == 0;
        double outer = r * 0.90;
        double inner = r * (quarter ? 0.68 : 0.78);
        cairo_set_line_width(cr, r * (quarter ? 0.10 : 0.06));
        cairo_set_source_rgba(cr, ink[0], ink[1], ink[2],
                              quarter ? 0.70 : 0.38);
        cairo_move_to(cr, cx + cos(a) * inner, cy + sin(a) * inner);
        cairo_line_to(cr, cx + cos(a) * outer, cy + sin(a) * outer);
        cairo_stroke(cr);
    }

    double sec  = tm.tm_sec;
    double minf = tm.tm_min + sec / 60.0;
    double hourf = (tm.tm_hour % 12) + minf / 60.0;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* Hour, then minute: the longer hand on top, so where they overlap the one
     * that moves is the one you can see. */
    double ha = hourf * M_PI / 6.0 - M_PI_2;
    cairo_set_line_width(cr, r * 0.16);
    cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.95);
    cairo_move_to(cr, cx - cos(ha) * r * 0.14, cy - sin(ha) * r * 0.14);
    cairo_line_to(cr, cx + cos(ha) * r * 0.50, cy + sin(ha) * r * 0.50);
    cairo_stroke(cr);

    double ma = minf * M_PI / 30.0 - M_PI_2;
    cairo_set_line_width(cr, r * 0.11);
    cairo_move_to(cr, cx - cos(ma) * r * 0.16, cy - sin(ma) * r * 0.16);
    cairo_line_to(cr, cx + cos(ma) * r * 0.78, cy + sin(ma) * r * 0.78);
    cairo_stroke(cr);

    /* The second hand follows the SAME setting the digits did — Clock & Time's
     * "Show seconds" — so the desktop has one answer about whether it counts
     * seconds, and a dock that is not animating a hand is not waking once a
     * second to redraw one. */
    if (s->clock.seconds) {
        double sa = sec * M_PI / 30.0 - M_PI_2;
        cairo_set_line_width(cr, r * 0.055);
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.95);
        cairo_move_to(cr, cx - cos(sa) * r * 0.22, cy - sin(sa) * r * 0.22);
        cairo_line_to(cr, cx + cos(sa) * r * 0.86, cy + sin(sa) * r * 0.86);
        cairo_stroke(cr);
    }

    /* The pin, over both hands — an accent dot when there is a second hand to
     * belong to, the ink when there is not. */
    if (s->clock.seconds)
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 1.0);
    else
        cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.95);
    cairo_arc(cr, cx, cy, r * 0.085, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
}

static void dock_draw_clock(syn_server_t *s, cairo_t *cr,
                            const dock_metrics_t *m)
{
    if (m->clk_w <= 0 || m->clk_h <= 0) return;

    /*
     * A hairline on each side of the cell that has something next to it, so the
     * clock reads as its own thing rather than as a very wide gap in the row.
     *
     * BOTH sides, now that the cell can be dragged into the middle: a rule on
     * the left alone was right for the only position the clock used to have and
     * wrong everywhere else. A side with no icon beyond it gets none — a rule
     * with nothing on one side of it is a mark on the end of the bar.
     */
    cairo_set_source_rgba(cr, s->config.panel_ink[0], s->config.panel_ink[1],
                          s->config.panel_ink[2], 0.22);
    cairo_set_line_width(cr, 1);
    for (int side = 0; side < 2; side++) {
        /* side 0 = the low end of the run, side 1 = the high end. */
        bool has_neighbour = side == 0
            ? m->clk_slot > 0
            : m->clk_slot < m->n || m->apps_s > 0 || m->pwr_s > 0;
        if (!has_neighbour) continue;

        double half = m->pad / 2.0;
        if (m->vertical) {
            double y = (side == 0 ? m->clk_y - half
                                  : m->clk_y + m->clk_h + half) + 0.5;
            cairo_move_to(cr, m->bx + 10, y);
            cairo_line_to(cr, m->bx + m->bw - 10, y);
        } else {
            double x = (side == 0 ? m->clk_x - half
                                  : m->clk_x + m->clk_w + half) + 0.5;
            cairo_move_to(cr, x, m->by + 10);
            cairo_line_to(cr, x, m->by + m->bh - 10);
        }
        cairo_stroke(cr);
    }

    /* The rules above belong to the CELL and are drawn either way; only what
     * goes inside it changes. */
    dock_clock_layout_t l;
    dock_clock_layout(s, m->vertical, m->thick, &l);
    if (l.analog) {
        dock_draw_clock_face(s, cr, m);
        return;
    }

    char time_s[32] = {0}, date_s[32] = {0};
    dock_clock_strings(s, m->vertical, time_s, sizeof time_s,
                       date_s, sizeof date_s);

    cairo_select_font_face(cr, syn_text_ui_font(), CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    double cx = m->clk_x + m->clk_w / 2.0;
    double cy = m->clk_y + m->clk_h / 2.0;

    const float *ink = s->config.panel_ink;

    if (m->vertical) {
        /*
         * STACKED FROM THE MIDDLE at the sizes dock_clock_layout() fitted, and
         * the two lines are placed against each other rather than at offsets
         * from the centre. The old code drew the date at a flat +13px from the
         * middle whatever size the time was, which is a gap that only looked
         * right at one font size — and once the time can shrink to fit the
         * column, there is no one size any more.
         */
        double total = l.t_px + DOCK_CLOCK_LINE_GAP + l.d_px;
        double top   = cy - total / 2.0;

        cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.95);
        dock_clock_line(cr, time_s, l.t_px, cx, top + l.t_px);
        cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.62);
        dock_clock_line(cr, date_s, l.d_px, cx, top + total);
        return;
    }

    /* Sized off the slab, so the whole cell grows with the Dock size row rather
     * than leaving a 17px time adrift in a 200px bar. At the stock 64 every one
     * of these is the literal it used to be. */
    cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.95);
    dock_clock_line(cr, time_s, l.t_px, cx, cy - dock_clock_px(1, m->thick));
    cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], 0.62);
    dock_clock_line(cr, date_s, l.d_px, cx, cy + dock_clock_px(13, m->thick));
}

static void dock_render_output(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    if (!s->config.dock_enabled) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return;
    int n = s->dock_entry_count;
    bool vertical = m.vertical;

    bool glass  = dock_style_is_glass(&s->config);
    /* On the BODY, which is what gets the rounded fill — see dock_bar_radius. */
    double radius = dock_bar_radius(s, m.bw, m.bh);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(m.w, m.h, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* The slab is drawn in BODY-local coordinates; everything after it is in
     * canvas coordinates, which is where dock_metrics() reports the cells. */
    cairo_save(cr);
    cairo_translate(cr, m.bx, m.by);
    dock_paint_body(s, cr, m.bw, m.bh, radius, glass);
    cairo_restore(cr);

    if (s->config.dock_clock) dock_draw_clock(s, cr, &m);

    /* Is an icon on THIS server being dragged, and to where. The dragged entry
     * is drawn last and elsewhere, so the loop below skips its cell. */
    bool dragging_icon = s->dock_drag.active && s->dock_drag.moved &&
                         s->dock_drag.icon >= 0 && s->dock_drag.icon < n;

    int order[DOCK_MAX_ENTRIES];
    int ndisp = dock_display_order(s, order, DOCK_MAX_ENTRIES);
    if (ndisp > m.n) ndisp = m.n;

    double now = dock_now();
    for (int slot = 0; slot < ndisp; slot++) {
        int i = order[slot];
        syn_dock_entry_t *e = &s->dock_entries[i];
        int ix = m.cx[slot], iy = m.cy[slot], size = m.cs[slot];

        /* The hit-box goes on the CELL, not on where the icon is painted: the
         * dragged icon is painted under the cursor, and a hit-box that followed
         * it would leave the gap it came out of clickable and the icon itself
         * hit-testable in mid-air. Nothing hit-tests during a drag anyway, and
         * on release the box is right for wherever the icon settled.
         *
         * ⚠ These are the LAST-RENDERED output's cells, and with magnification
         * the mirrors genuinely disagree — so nothing that has an output in hand
         * may read them. dock_entry_at() and dock_icon_drag_begin() re-derive
         * from dock_metrics() for the output they are asking about; what is left
         * here is the flat rect every mirror agrees on during a drag, which is
         * the only time it is read. */
        e->x = ix; e->y = iy; e->w = size; e->h = size;

        if (dragging_icon && i == s->dock_drag.icon) continue;   /* drawn below */

        /* Press-pop: scale the icon about its centre. Only the icon glyph is
         * transformed — the hit-box and running-dot stay put. */
        dock_draw_icon(cr, e->app_id, ix, iy, size, m.icon,
                       dock_click_scale(e, now));

        if (e->running) {
            double dx, dy;
            /* Off the FLAT cell, not the swollen one: the dot marks the app's
             * place in the row and a dot that slid up the screen with the icon
             * would read as part of the icon rather than as a mark on the bar. */
            int flat = m.icon;
            /* The same two nudges dock_cell_cross() uses, as fractions of the
             * icon: at the stock 48 they are the 4 and the 6 they were literals
             * for, and on a 184px icon the dot is still just under it rather
             * than buried a third of the way up the picture. */
            double lift = m.icon / 12.0, gap = m.icon / 8.0;
            if (vertical) {
                /* Dot on the inner long edge of the column. */
                dx = (s->config.dock_edge == SYN_DOCK_EDGE_LEFT)
                         ? m.bx + m.bw - 5.0 : m.bx + 5.0;
                dy = iy + size / 2.0;
            } else {
                dx = ix + size / 2.0;
                dy = m.by + (m.thick + flat) / 2.0 - lift + gap;
            }
            /* panel_ink, not a white literal: now that the body follows the
             * theme, a light theme (XP's beige, 95's silver) draws a near-white
             * dot on a near-white bar and the "app is running" mark disappears.
             * The ink is by definition the colour that reads on that surface.
             * Costs stock a hair — SYNAPSE's ink is 0.95/0.95/1.00 against the
             * old 0.92/0.92/0.96 — which is below noticing on a 2.5px dot. */
            cairo_set_source_rgba(cr, s->config.panel_ink[0],
                                  s->config.panel_ink[1],
                                  s->config.panel_ink[2], 0.9);
            cairo_arc(cr, dx, dy, m.icon / 19.2, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    /* The show-all-apps button, after the icons and outside their loop: it is
     * not an entry, it has no app_id and no running dot, and the one thing it
     * shares with them is the cell it is drawn in. */
    if (m.apps_s > 0)
        dock_draw_apps(s, cr, m.apps_x, m.apps_y, m.apps_s);
    /* Same again for the power button, which is the same kind of thing: a cell
     * with no app_id behind it, past the apps button at the end of the run. */
    if (m.pwr_s > 0)
        dock_draw_power(s, cr, m.pwr_x, m.pwr_y, m.pwr_s);

    /* The lifted icon, last so it is over everything, and slightly larger with a
     * shadow under it. Both say "this one is off the surface" — without them a
     * dragged icon and a settled one are the same picture in different places,
     * and which one the pointer is carrying stops being obvious the moment it
     * lines up with a gap. */
    if (dragging_icon) {
        syn_dock_entry_t *e = &s->dock_entries[s->dock_drag.icon];
        double ix = s->dock_drag.icon_x, iy = s->dock_drag.icon_y;
        int icon = m.icon;
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
        cairo_arc(cr, ix + icon / 2.0, iy + icon * 0.86, icon * 0.40, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
        dock_draw_icon(cr, e->app_id, ix, iy, icon, m.icon, 1.12);
    }

    cairo_destroy(cr);
    set_scene_buffer(&o->dock.icons_buf, o->dock.tree, buf);
    o->dock.clock_drawn = dock_clock_stamp(s);

    /* Frosted glass is what the blur does behind the buffer, not what the fill
     * does on it — see dock_paint_body(). Gated on the user's master blur switch
     * as well as the style: somebody who turned blur off for their windows did
     * not mean "except the dock". */
    syn_buffer_backdrop_blur(o->dock.icons_buf, glass && s->config.blur,
                             (int)lround(radius));

    /* Position/visibility follow the current slide state, not a forced
     * "shown" — the auto-hide tick owns whether the bar is on-screen. */
    dock_apply_position(o);
}

void dock_relayout(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        dock_render_output(o);
}

/* Schedule a frame on every output so dock_tick re-evaluates at once. Used
 * after a setting changes (auto-hide toggled) so the bar pins or releases
 * immediately rather than waiting on the next incidental repaint. */
void dock_wake(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);
}

/* ── Public API ──────────────────────────────────────────── */

/*
 * The dock clock's heartbeat.
 *
 * The rest of the dock is driven by output frames, and output frames stop when
 * nothing on screen is moving — which is precisely the state a clock has to keep
 * ticking through. So the clock gets a timer of its own rather than a `return
 * true` out of dock_tick(), which would hold every output at its refresh rate
 * for the sake of one string a minute.
 *
 * It only wakes the outputs; dock_tick() decides whether the string has actually
 * changed (dock_clock_due) and repaints if so. Re-arms unconditionally so the
 * clock can be switched on mid-session without anything having to re-arm it.
 */
#define DOCK_CLOCK_POLL_MS 1000

static int dock_clock_tick(void *data)
{
    syn_server_t *s = data;
    if (s->config.dock_enabled && s->config.dock_clock) dock_wake(s);
    wl_event_source_timer_update(s->dock_clock_timer, DOCK_CLOCK_POLL_MS);
    return 0;
}

void dock_init(syn_server_t *s)
{
    s->dock_entry_count = 0;
    /* -1 is "no icon", and the server struct is zeroed — which would read as
     * "entry 0". Nothing looks at it while the drag is inactive, but a field
     * whose resting value is a valid index is one refactor from being read. */
    s->dock_drag.icon = DOCK_DRAG_BAR;
    dock_rebuild(s);   /* seeds pinned-only entries; nothing mapped yet */

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->dock_clock_timer = wl_event_loop_add_timer(loop, dock_clock_tick, s);
    if (s->dock_clock_timer)
        wl_event_source_timer_update(s->dock_clock_timer, DOCK_CLOCK_POLL_MS);
}

void dock_finish(syn_server_t *s)
{
    /* Process-lifetime, but freed anyway: the leak checker in the sanitizer
     * build does not know "one allocation, held on purpose" from a bug, and a
     * suppression is a worse answer than a destructor. */
    if (dock_measure) {
        cairo_destroy(dock_measure);
        dock_measure = NULL;
    }
    dock_icon_cache_flush();
    if (s->dock_clock_timer) {
        wl_event_source_remove(s->dock_clock_timer);
        s->dock_clock_timer = NULL;
    }
}

void dock_output_created(syn_output_t *o)
{
    o->dock.tree = wlr_scene_tree_create(&o->server->scene->tree);
    /* Auto-hide: start hidden (pushed off the edge). The tick slides it in
     * when the cursor reaches the trigger strip. */
    o->dock.shown = 0;
    o->dock.slide_progress = 0.0;
    o->dock.hover_since = 0.0;
    o->dock.unhover_since = 0.0;
    o->dock.last_tick = 0.0;
    o->dock.mag_run = 0.0;
    o->dock.mag_amount = 0.0;
    o->dock.mag_want = 0;
    o->dock.clock_drawn = -1;   /* nothing drawn yet — never a real stamp */
    dock_render_output(o);
}

void dock_output_destroy(syn_output_t *o)
{
    if (o->server->dock_drag.output == o) {
        o->server->dock_drag.active = 0;
        o->server->dock_drag.moved = 0;
        o->server->dock_drag.icon = -1;
        o->server->dock_drag.output = NULL;
    }
    if (o->dock.tree) {
        wlr_scene_node_destroy(&o->dock.tree->node);
        o->dock.tree = NULL;
        o->dock.icons_buf = NULL;
    }
}

/*
 * Is the dock the topmost thing at (lx,ly) on this output?
 *
 * With `dock_on_top` off the dock sits BELOW window_tree, so a window can be in
 * front of it — and every question the dock asks about the pointer has to be
 * asked of a dock that can actually be seen there. Without this the bar stayed
 * fully clickable through whatever covered it: a right-click on a maximized
 * window's bottom strip opened the dock's menu instead of the app's.
 *
 * surface_at() walks the scene graph, so it already answers this in the dock's
 * own stacking terms: with the dock on top its buffer is the topmost node and
 * is not a wl_surface, so the call returns NULL and every window under it is
 * correctly invisible to the test. Which is why the check is SKIPPED when the
 * dock floats — asking it then would find the window the dock is covering and
 * refuse a click the user can plainly see landing on the dock.
 */
static bool dock_point_clear(syn_server_t *s, syn_output_t *o, double lx, double ly)
{
    if (dock_on_top_here(o)) return true;
    double sx, sy;
    return surface_at(s, lx, ly, NULL, &sx, &sy) == NULL;
}

/* Would the clock draw a different string than the one on this output's canvas?
 * False when the clock is off, so the 1 Hz wake costs nothing on a dock without
 * one. See dock_clock_stamp(). */
static bool dock_clock_due(syn_output_t *o)
{
    syn_server_t *s = o->server;
    return s->config.dock_clock &&
           o->dock.clock_drawn != dock_clock_stamp(s);
}

/* ── Auto-hide ───────────────────────────────────────────── */

/* Advance this output's slide animation one frame and re-evaluate hover.
 * Returns true while more frames are needed (mid-slide, or shown-and-waiting
 * for the hide delay to elapse) so output_frame keeps scheduling. `now` is
 * CLOCK_MONOTONIC seconds. */
bool dock_tick(syn_output_t *o, double now)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return false;

    /* While this output's dock is being dragged, the drag owns its position
     * and it stays shown — no auto-hide work. Keep frames coming. */
    if (s->dock_drag.active && s->dock_drag.output == o) {
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
        return true;
    }

    if (!s->config.dock_enabled) {
        if (o->dock.shown || o->dock.slide_progress != 0.0) {
            o->dock.shown = 0;
            o->dock.slide_progress = 0.0;
            o->dock.last_tick = 0.0;
            dock_apply_position(o);
        }
        return false;
    }

    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return false;

    double cx = s->cursor->x, cy = s->cursor->y;
    bool on_output = cx >= m.ob.x && cx < m.ob.x + m.ob.width &&
                     cy >= m.ob.y && cy < m.ob.y + m.ob.height;

    /* The canvas as it currently sits, so "over the bar" is asked of where the
     * bar actually is rather than of where it would be fully shown. */
    double ox = o->dock.tree->node.x, oy = o->dock.tree->node.y;
    bool on_screen = o->dock.shown || o->dock.slide_progress > 0.0;
    bool over_canvas = on_screen && on_output &&
                       cx >= ox && cx < ox + m.w &&
                       cy >= oy && cy < oy + m.h;

    /* ── Magnification ──
     *
     * The centre is the pointer's position on the FLAT run — measured from
     * base_origin, which does not move when the magnified bar grows. Feeding
     * the live canvas origin in instead makes the layout an input to itself and
     * the row oscillates.
     *
     * It also has to survive the dock being covered: with `dock_on_top` off a
     * window can be in front of the bar, and swelling icons under it would be a
     * dock reacting to a pointer that is demonstrably doing something else. */
    bool anim_mag = false;
    if (s->config.dock_magnify) {
        bool want = over_canvas && dock_point_clear(s, o, cx, cy);
        double run = (m.vertical ? cy : cx) - m.base_origin;
        double goal = want ? 1.0 : 0.0;

        if (want && fabs(run - o->dock.mag_run) >= 1.0) {
            o->dock.mag_run = run;
            if (o->dock.mag_amount > 0.0) anim_mag = true;
        }
        if (o->dock.mag_amount != goal) {
            double dt = (o->dock.last_tick > 0.0) ? now - o->dock.last_tick : 0.0;
            if (dt <= 0.0 || dt > 0.5) dt = 0.016;
            double step = dt / DOCK_MAG_EASE_SECS;
            if (o->dock.mag_amount < goal)
                o->dock.mag_amount = fmin(goal, o->dock.mag_amount + step);
            else
                o->dock.mag_amount = fmax(goal, o->dock.mag_amount - step);
            anim_mag = true;
        }
        o->dock.mag_want = want;
    } else if (o->dock.mag_amount != 0.0) {
        o->dock.mag_amount = 0.0;
        anim_mag = true;
    }

    /* Always-visible mode: pin the bar on screen and skip the hover state
     * machine entirely — the same fixed pose the drag branch holds, but
     * permanent. It still floats above content rather than reserving layout
     * space, so nothing else has to be re-laid-out when this toggles. */
    if (!s->config.dock_autohide) {
        if (!o->dock.shown || o->dock.slide_progress != 1.0) {
            o->dock.shown = 1;
            o->dock.slide_progress = 1.0;
            o->dock.unhover_since = 0.0;
            o->dock.last_tick = 0.0;
            dock_apply_position(o);
        }
        /* Icons still press-pop on click; keep rendering while one animates. */
        bool clicking = false;
        for (int i = 0; i < s->dock_entry_count; i++)
            if (dock_entry_animating(&s->dock_entries[i], now)) { clicking = true; break; }
        bool ticking = dock_clock_due(o);
        if (clicking || anim_mag || ticking) {
            if (anim_mag) o->dock.last_tick = now;
            dock_render_output(o);
        }
        return clicking || anim_mag;
    }

    int margin = s->config.dock_hover_margin;
    if (margin < 1) margin = 1;
    int pad = dock_icon_pad(&s->config);
    syn_dock_edge_t edge = s->config.dock_edge;
    /* The fully-shown BODY, which is the strip the reveal trigger runs along. */
    int bx = m.x + m.bx, by = m.y + m.by, bw = m.bw, bh = m.bh;

    /* Reveal trigger: a `margin`-thick strip along the dock's edge, within
     * the bar's footprint (plus a little padding on the long axis). */
    bool in_trigger = false;
    if (on_output) {
        switch (edge) {
        case SYN_DOCK_EDGE_TOP:
            in_trigger = cy < m.ob.y + margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        case SYN_DOCK_EDGE_LEFT:
            in_trigger = cx < m.ob.x + margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_RIGHT:
            in_trigger = cx >= m.ob.x + m.ob.width - margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_BOTTOM:
        default:
            in_trigger = cy >= m.ob.y + m.ob.height - margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        }
    }
    /* Keep-shown region: anywhere over the bar, but only once some of it is
     * actually on screen. The CANVAS and not the body, so a magnified icon
     * standing out past the slab keeps the dock out while the pointer is on it.
     * dock_metrics() reports the *fully-shown* rect, so testing it
     * unconditionally would treat the whole dock_height band as a reveal
     * trigger and make `margin` meaningless. */
    bool in_bar = over_canvas;

    /* Don't summon a hidden dock mid-drag: a client holding an implicit
     * pointer grab (rubber-band select, window drag) owns the cursor, and
     * sliding the bar out from under it only covers what it is aimed at. */
    if (!on_screen && s->seat && s->seat->pointer_state.button_count > 0)
        in_trigger = false;

    /* The trigger strip only counts once the cursor has rested in it for
     * DOCK_REVEAL_DELAY — a cursor merely passing through on its way to the
     * next monitor never stays that long. A dock that is already out stays
     * out with no dwell, so re-entering the strip mid-hide is instant. */
    if (in_trigger) {
        if (o->dock.hover_since == 0.0)
            o->dock.hover_since = now;
    } else {
        o->dock.hover_since = 0.0;
    }
    bool dwelt = in_trigger && (o->dock.shown ||
                                now - o->dock.hover_since >= DOCK_REVEAL_DELAY);

    bool engaged = dwelt || in_bar;

    if (engaged) {
        o->dock.unhover_since = 0.0;
        o->dock.shown = 1;
    } else if (o->dock.shown) {
        if (o->dock.unhover_since == 0.0)
            o->dock.unhover_since = now;
        if (now - o->dock.unhover_since >= DOCK_HIDE_DELAY)
            o->dock.shown = 0;
    }

    double goal = o->dock.shown ? 1.0 : 0.0;
    if (o->dock.slide_progress != goal) {
        double dt = (o->dock.last_tick > 0.0) ? now - o->dock.last_tick : 0.0;
        if (dt <= 0.0 || dt > 0.5) dt = 0.016;   /* first frame / stall guard */
        o->dock.last_tick = now;

        double step = dt / DOCK_SLIDE_SECS;
        if (o->dock.slide_progress < goal)
            o->dock.slide_progress = fmin(goal, o->dock.slide_progress + step);
        else
            o->dock.slide_progress = fmax(goal, o->dock.slide_progress - step);

        dock_apply_position(o);
    } else if (anim_mag) {
        o->dock.last_tick = now;
    } else {
        o->dock.last_tick = 0.0;
    }

    /* Click press-pop: while any icon is mid-animation, re-render this output's
     * dock canvas each frame (the slide path only repositions the tree, it
     * doesn't repaint the icons) and keep frames coming until it settles. */
    bool clicking = false;
    for (int i = 0; i < s->dock_entry_count; i++) {
        if (dock_entry_animating(&s->dock_entries[i], now)) { clicking = true; break; }
    }
    /* A magnification step changes the SIZE of the canvas, so unlike the slide
     * it has to go through a full repaint, not just a reposition. */
    if ((clicking || anim_mag || dock_clock_due(o)) && on_screen)
        dock_render_output(o);

    bool animating = o->dock.slide_progress != goal;
    bool waiting_to_hide = !engaged && o->dock.shown;
    /* Mid-dwell nothing is moving, but the cursor may have stopped dead in the
     * strip — dock_pointer_motion() won't wake us again, so keep frames coming
     * until the dwell elapses or the cursor leaves. */
    bool waiting_to_show = in_trigger && !engaged;
    return animating || waiting_to_hide || waiting_to_show || clicking || anim_mag;
}

/* Pointer moved: wake the outputs whose dock might need to react (cursor near
 * the dock's edge, a dock already on-screen, or an active drag). dock_tick
 * does the actual state work on the frame this schedules. */
void dock_pointer_motion(syn_server_t *s)
{
    if (!s->config.dock_enabled) return;

    double cx = s->cursor->x, cy = s->cursor->y;
    /* Past the body AND past the magnification headroom: with magnify on, a
     * pointer 20px above a bottom dock is already swelling icons, so a band
     * that stopped at the slab would leave the effect waiting on the next
     * incidental repaint to notice the pointer at all. */
    int band = s->config.dock_height + dock_headroom(&s->config) + 8;
    syn_dock_edge_t edge = s->config.dock_edge;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree) continue;
        struct wlr_box ob;
        output_box_of(s, o, &ob);
        bool on_x = cx >= ob.x && cx < ob.x + ob.width;
        bool on_y = cy >= ob.y && cy < ob.y + ob.height;
        bool near_edge = false;
        switch (edge) {
        case SYN_DOCK_EDGE_TOP:
            near_edge = on_x && cy < ob.y + band; break;
        case SYN_DOCK_EDGE_LEFT:
            near_edge = on_y && cx < ob.x + band; break;
        case SYN_DOCK_EDGE_RIGHT:
            near_edge = on_y && cx >= ob.x + ob.width - band; break;
        case SYN_DOCK_EDGE_BOTTOM:
        default:
            near_edge = on_x && cy >= ob.y + ob.height - band; break;
        }
        if (near_edge || o->dock.shown ||
            (s->dock_drag.active && s->dock_drag.output == o))
            wlr_output_schedule_frame(o->wlr_output);
    }
}

/* ── Hit-testing ─────────────────────────────────────────── */

syn_dock_entry_t *dock_entry_at(syn_server_t *s, double lx, double ly)
{
    if (!s->config.dock_enabled) return NULL;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        if (!dock_point_clear(s, o, lx, ly)) continue;

        /* Cell rects are dock-canvas-local and, since magnification, are the
         * property of ONE output rather than of the entry — so they are derived
         * here for the mirror being tested instead of read off s->dock_entries.
         * The tree's scene position is that canvas's layout-coordinate origin
         * (which already reflects any slide/float). */
        dock_metrics_t m;
        if (!dock_metrics(o, &m)) continue;

        double rx = lx - o->dock.tree->node.x;
        double ry = ly - o->dock.tree->node.y;

        int order[DOCK_MAX_ENTRIES];
        int ndisp = dock_display_order(s, order, DOCK_MAX_ENTRIES);
        if (ndisp > m.n) ndisp = m.n;

        /* Back to front along the run: a swollen icon overlaps the gaps either
         * side of it, and the last one drawn is the one the pointer is on. */
        for (int slot = ndisp - 1; slot >= 0; slot--) {
            if (rx >= m.cx[slot] && rx < m.cx[slot] + m.cs[slot] &&
                ry >= m.cy[slot] && ry < m.cy[slot] + m.cs[slot])
                return &s->dock_entries[order[slot]];
        }
    }
    return NULL;
}

/* The cell the entry at `idx` is currently DRAWN in on this output, canvas-local.
 * The drag needs it because the icon it lifts may be a magnified one, and
 * grabbing it by its flat rect makes it jump the moment the button goes down. */
static bool dock_entry_cell(syn_output_t *o, int idx,
                            int *ix, int *iy, int *size)
{
    syn_server_t *s = o->server;
    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return false;

    int order[DOCK_MAX_ENTRIES];
    int ndisp = dock_display_order(s, order, DOCK_MAX_ENTRIES);
    if (ndisp > m.n) ndisp = m.n;

    for (int slot = 0; slot < ndisp; slot++) {
        if (order[slot] != idx) continue;
        *ix = m.cx[slot]; *iy = m.cy[slot]; *size = m.cs[slot];
        return true;
    }
    return false;
}

/* The output whose dock CANVAS covers this point. dock_bar_at() answers for the
 * body alone, which is the right question for an edge-drag and the wrong one for
 * an icon: a magnified icon stands up out of the slab, so the press that lifts
 * it can land entirely in the headroom. */
static syn_output_t *dock_canvas_at(syn_server_t *s, double lx, double ly)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        dock_metrics_t m;
        if (!dock_metrics(o, &m)) continue;
        double ox = o->dock.tree->node.x, oy = o->dock.tree->node.y;
        if (lx >= ox && lx < ox + m.w && ly >= oy && ly < oy + m.h)
            return o;
    }
    return NULL;
}

bool dock_bar_at(syn_server_t *s, double lx, double ly, syn_output_t **out)
{
    if (!s->config.dock_enabled) return false;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        if (!dock_point_clear(s, o, lx, ly)) continue;
        dock_metrics_t m;
        if (!dock_metrics(o, &m)) continue;
        /* The BODY, not the canvas: the magnification headroom is transparent,
         * and a press in it must not start an edge-drag of a bar that visibly
         * is not there. Icons standing up into it are caught by dock_entry_at(),
         * which every caller runs first. */
        double ox = o->dock.tree->node.x + m.bx;
        double oy = o->dock.tree->node.y + m.by;
        if (lx >= ox && lx < ox + m.bw && ly >= oy && ly < oy + m.bh) {
            if (out) *out = o;
            return true;
        }
    }
    return false;
}

/*
 * The cells that are not apps, hit-tested the same way an icon is: off the
 * metrics for the mirror being asked about, never off a rect cached on anything
 * server-global. Only the screen the pointer is on magnifies, so the cells
 * genuinely differ between mirrors.
 *
 * All of them have to be asked BEFORE dock_bar_at(), and that is not a detail:
 * they are drawn on the body, so every press that lands on one of them also
 * lands on the bar. Asked the other way round, the apps and power buttons would
 * start an edge-drag of the whole dock and the clock could never be picked up
 * at all.
 *
 * ⚠ AN ENUM AND NOT A BOOL, and it used to be a bool. `clock` meant "the clock,
 * otherwise the apps button", so the moment a third cell existed the false case
 * would have silently answered for the wrong one — the same shape as the dock
 * drag's `icon < 0`, which cost a release. Adding a fourth cell means adding a
 * case here and the compiler saying where else.
 */
typedef enum { DOCK_CELL_CLOCK, DOCK_CELL_APPS, DOCK_CELL_POWER } dock_cell_t;

static bool dock_cell_hit(syn_server_t *s, double lx, double ly,
                          dock_cell_t which, syn_output_t **out)
{
    if (!s->config.dock_enabled) return false;
    switch (which) {
    case DOCK_CELL_CLOCK: if (!s->config.dock_clock)        return false; break;
    case DOCK_CELL_APPS:  if (!s->config.dock_apps_button)  return false; break;
    case DOCK_CELL_POWER: if (!s->config.dock_power_button) return false; break;
    }

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        if (!dock_point_clear(s, o, lx, ly)) continue;
        dock_metrics_t m;
        if (!dock_metrics(o, &m)) continue;

        double rx = lx - o->dock.tree->node.x;
        double ry = ly - o->dock.tree->node.y;
        int cx, cy, cw, ch;
        switch (which) {
        case DOCK_CELL_CLOCK:
            if (m.clk_w <= 0) continue;
            cx = m.clk_x; cy = m.clk_y; cw = m.clk_w; ch = m.clk_h;
            break;
        case DOCK_CELL_APPS:
            if (m.apps_s <= 0) continue;
            cx = m.apps_x; cy = m.apps_y; cw = m.apps_s; ch = m.apps_s;
            break;
        case DOCK_CELL_POWER:
        default:
            if (m.pwr_s <= 0) continue;
            cx = m.pwr_x; cy = m.pwr_y; cw = m.pwr_s; ch = m.pwr_s;
            break;
        }
        if (rx >= cx && rx < cx + cw && ry >= cy && ry < cy + ch) {
            if (out) *out = o;
            return true;
        }
    }
    return false;
}

bool dock_apps_at(syn_server_t *s, double lx, double ly)
{
    return dock_cell_hit(s, lx, ly, DOCK_CELL_APPS, NULL);
}

bool dock_clock_at(syn_server_t *s, double lx, double ly)
{
    return dock_cell_hit(s, lx, ly, DOCK_CELL_CLOCK, NULL);
}

bool dock_power_at(syn_server_t *s, double lx, double ly)
{
    return dock_cell_hit(s, lx, ly, DOCK_CELL_POWER, NULL);
}

/* How long a tray restore gets before we call it wedged. steam://open/main
 * brings a healthy client back in ~2s (measured, 3/3), so this is generous —
 * deliberately, because the fallback tears the window down and rebuilding it is
 * far more disruptive than waiting. */
#define DOCK_UNWEDGE_DELAY_MS 6000

static bool dock_app_is_mapped(syn_server_t *s, const char *app_id)
{
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            if (!v->mapped) continue;
            const char *a = view_app_id(v);
            if (a && strcmp(a, app_id) == 0) return true;
        }
    }
    return false;
}

/*
 * The restore command went out; did a window actually come back?
 *
 * The dock cannot tell "sitting in the tray" from "wedged" at click time —
 * both are simply "no mapped view", which is why the open/main fix reads as
 * flaky: it is the right thing for the first and useless for the second. So
 * ask the only question that distinguishes them, which is whether a window
 * appeared, and only then reach for the disruptive remedy.
 */
static int dock_unwedge_cb(void *data)
{
    syn_server_t *s = data;
    if (dock_app_is_mapped(s, "steam")) return 0;   /* restored normally */
    /* Deliberately re-derived rather than captured at arm time: six seconds is
     * long enough for the client to have exited and taken its view with it. */
    xwayland_unwedge(s, "steam", "Steam");
    return 0;
}

static void dock_arm_unwedge(syn_server_t *s)
{
    if (!s->dock_unwedge_timer) {
        struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
        s->dock_unwedge_timer = wl_event_loop_add_timer(loop, dock_unwedge_cb, s);
        if (!s->dock_unwedge_timer) return;
    }
    /* Re-arming an already-pending timer just pushes it out, which is what
     * repeated clicking should do. */
    wl_event_source_timer_update(s->dock_unwedge_timer, DOCK_UNWEDGE_DELAY_MS);
}

void dock_entry_click(syn_server_t *s, syn_dock_entry_t *e)
{
    /* Kick off the press-pop and wake every output's dock so the animation
     * actually plays (dock_tick re-renders while an entry is animating). */
    e->anim_start = dock_now();
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);

    /* The dock groups every window of an app under one icon, so a click has to
     * act on the whole group. Keying it off a single stashed view (primary_view)
     * is exactly what stranded a second window: minimize the one the icon
     * pointed at and it would re-point at the other instance, leaving the
     * minimized one with no icon that could bring it back. Gather the app's live
     * windows first — into a local array, so restoring or minimizing them cannot
     * reorder a workspace list we are still walking — and decide from the set. */
    syn_view_t *inst[64];
    int ninst = 0, any_minimized = 0, app_has_focus = 0;
    syn_view_t *focus_target = NULL, *ws_ref = NULL, *v;
    for (int wi = 0; wi < WORKSPACE_MAX; wi++)
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            const char *a;
            if (!v->mapped) continue;
            a = view_app_id(v);
            if (!a || strcmp(a, e->app_id) != 0) continue;
            ws_ref = v;
            if (v->minimized) {
                any_minimized = 1;
            } else {
                if (!focus_target) focus_target = v;
                if (v == s->focused_view) app_has_focus = 1;
            }
            if (ninst < (int)(sizeof inst / sizeof inst[0])) inst[ninst++] = v;
        }

    /* No window: either it is not running, or it is sitting in the tray with
     * its window unmapped — the dock cannot tell those apart, because both look
     * like "no mapped view". Ask the app. */
    if (ninst == 0) {
        const char *restore = dock_tray_restore_exec(e->app_id);
        if (restore) {
            wlr_log(WLR_INFO, "dock: %s is tray-resident, restoring via: %s",
                    e->app_id, restore);
            synui_spawn(restore);
            /* ...and check up on it: the command is a no-op against a wedged
             * client, and this click is the only signal that the user wants
             * the window now. */
            if (strcmp(e->app_id, "steam") == 0) dock_arm_unwedge(s);
            return;
        }
        const syn_icon_entry_t *ic = icon_lookup(e->app_id);
        if (ic->exec[0]) synui_spawn(ic->exec);
        return;
    }

    /* Restore and focus both need the app's workspace on screen. */
    if (ws_ref->workspace && !workspace_visible(ws_ref->workspace))
        workspace_switch(s, ws_ref->workspace->index);

    /* Any hidden instance → raise the group. Restoring *every* minimized window,
     * not just one, is what keeps a duplicate from being stranded; the last one
     * restored takes focus, as view_apply_minimized() raises+focuses on restore
     * (mirrors ft_handle_minimize). */
    if (any_minimized) {
        for (int i = 0; i < ninst; i++)
            if (inst[i]->minimized) view_apply_minimized(s, inst[i], 0);
        return;
    }

    /* Every instance is already shown. If one holds focus the app is fully
     * forward, so the click tucks the whole group away — the counterpart to
     * raising it, and every window is back one more click on the icon.
     * Otherwise the app is behind something: bring it to the front. */
    if (app_has_focus) {
        for (int i = 0; i < ninst; i++)
            view_apply_minimized(s, inst[i], 1);
        return;
    }
    focus_view(s, focus_target, view_surface(focus_target));
}

/* ── Drag to reposition ──────────────────────────────────── */

void dock_drag_begin(syn_server_t *s, double lx, double ly)
{
    syn_output_t *o = NULL;
    if (!dock_bar_at(s, lx, ly, &o) || !o) return;

    s->dock_drag.active  = 1;
    s->dock_drag.moved   = 0;
    s->dock_drag.icon    = DOCK_DRAG_BAR;   /* the bar, not a cell */
    s->dock_drag.output  = o;
    s->dock_drag.start_x = lx;
    s->dock_drag.start_y = ly;
}

/* A left press on an icon. Arms the rearrange; whether it turns out to be one is
 * settled on release, because until the pointer moves this is a click. */
void dock_icon_drag_begin(syn_server_t *s, syn_dock_entry_t *e,
                          double lx, double ly)
{
    if (!e) return;

    /* Index rather than the pointer: dock_rebuild() memcpy's a whole fresh array
     * over s->dock_entries on any map/unmap, so a stashed syn_dock_entry_t* is
     * live but the entry it points at can become a different app mid-gesture.
     * The index has the same problem in principle and is at least bounds-
     * checkable; dock_icon_drag_end() re-reads the app_id through it. */
    int idx = (int)(e - s->dock_entries);
    if (idx < 0 || idx >= s->dock_entry_count) return;

    /* The CANVAS, not the body — dock_canvas_at() says why. */
    syn_output_t *o = dock_canvas_at(s, lx, ly);
    if (!o) return;

    s->dock_drag.active  = 1;
    s->dock_drag.moved   = 0;
    s->dock_drag.icon    = idx;
    snprintf(s->dock_drag.icon_app, sizeof(s->dock_drag.icon_app),
             "%s", e->app_id);
    s->dock_drag.slot    = idx;
    s->dock_drag.output  = o;
    s->dock_drag.start_x = lx;
    s->dock_drag.start_y = ly;
    /* Where in the icon the press landed, so the lifted icon stays under the
     * same point of itself instead of jumping its centre to the cursor.
     *
     * Off the cell as DRAWN on this output, not off e->x/e->y: with magnify on
     * the icon being grabbed is a swollen one standing out of the slab, and its
     * flat rect is neither where it looks nor how big it looks. The lifted icon
     * is drawn flat (dock_metrics() suppresses magnification for the length of
     * the drag), so the grab offset is scaled back to the flat cell — otherwise
     * a press near the top of a 77px icon grabs past the bottom of the 48px one
     * it becomes. */
    int flat = dock_icon_size(&s->config);
    int cix = e->x, ciy = e->y, csz = e->w > 0 ? e->w : flat;
    dock_entry_cell(o, idx, &cix, &ciy, &csz);
    double scale = csz > 0 ? (double)flat / csz : 1.0;
    s->dock_drag.grab_dx = (lx - o->dock.tree->node.x - cix) * scale;
    s->dock_drag.grab_dy = (ly - o->dock.tree->node.y - ciy) * scale;
    s->dock_drag.icon_x  = cix;
    s->dock_drag.icon_y  = ciy;
}

/*
 * A left press on the clock. Same shape as the icon rearrange above and for the
 * same reason: until the pointer moves this is just a press, and the clock has
 * no click of its own to owe on release.
 *
 * `slot` starts at wherever the clock already is, so a press that never travels
 * commits nothing — the release compares nothing and writes nothing.
 */
void dock_clock_drag_begin(syn_server_t *s, double lx, double ly)
{
    syn_output_t *o = NULL;
    /* ⚠ DOCK_CELL_CLOCK by name. This read `true` while the parameter was a
     * bool meaning "the clock, otherwise the apps button", and `true` is 1 —
     * which is now DOCK_CELL_APPS. The compiler converts a bool to an enum
     * without a word, so the clock drag silently began hit-testing the apps
     * button and a press on the clock armed nothing at all. */
    if (!dock_cell_hit(s, lx, ly, DOCK_CELL_CLOCK, &o) || !o) return;

    s->dock_drag.active  = 1;
    s->dock_drag.moved   = 0;
    s->dock_drag.icon    = DOCK_DRAG_CLOCK;
    s->dock_drag.icon_app[0] = '\0';
    s->dock_drag.slot    = dock_clock_slot(s, s->dock_entry_count);
    s->dock_drag.output  = o;
    s->dock_drag.start_x = lx;
    s->dock_drag.start_y = ly;
}

/*
 * The clock does not get LIFTED the way an icon does, and that is deliberate.
 *
 * A lifted icon needs a picture under the cursor because the gap it came out of
 * looks exactly like the gap it is going into. The clock's cell is four times
 * as wide as anything around it — watching it jump from one gap to the next IS
 * the feedback, and a second copy of the time floating over the bar would be two
 * clocks disagreeing about where the clock is.
 */
static void dock_clock_drag_motion(syn_server_t *s, syn_output_t *o,
                                   double lx, double ly)
{
    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return;

    /* Relative to the BODY's origin, which on the run axis is the canvas's —
     * the headroom only ever insets the cross axis. */
    double run = m.vertical ? ly - o->dock.tree->node.y - m.by
                            : lx - o->dock.tree->node.x - m.bx;

    int slot = dock_clock_slot_at(&m, run);
    if (slot == s->dock_drag.slot) return;   /* nothing to repaint */
    s->dock_drag.slot = slot;

    /* Every mirror: the slot is server-global, so the cell has to move on all of
     * them or two monitors disagree for the length of the gesture. */
    dock_relayout(s);
    syn_output_t *out;
    wl_list_for_each(out, &s->outputs, link)
        if (out->wlr_output) wlr_output_schedule_frame(out->wlr_output);
}

/* The lifted icon follows the cursor, and the run-axis position of its centre
 * says which cell it wants. Clamped to the bar: this gesture rearranges, it does
 * not remove — dragging an icon off the dock would need somewhere for it to go
 * and a way to say so, and the right-click menu already unpins. */
static void dock_icon_drag_motion(syn_server_t *s, syn_output_t *o,
                                  double lx, double ly)
{
    dock_metrics_t m;
    if (!dock_metrics(o, &m)) return;

    double ix = lx - o->dock.tree->node.x - s->dock_drag.grab_dx;
    double iy = ly - o->dock.tree->node.y - s->dock_drag.grab_dy;

    /* Clamped to the BODY, not the canvas. The magnification headroom is
     * transparent, and an icon parked in it would be a picture floating clear of
     * the bar it is being rearranged inside. */
    double min_x = m.bx + m.pad, min_y = m.by + m.pad;
    double max_x = m.bx + m.bw - m.pad - m.icon;
    double max_y = m.by + m.bh - m.pad - m.icon;
    if (ix < min_x) ix = min_x;
    if (iy < min_y) iy = min_y;
    if (ix > max_x) ix = max_x > min_x ? max_x : min_x;
    if (iy > max_y) iy = max_y > min_y ? max_y : min_y;

    /* A high-polling-rate mouse sends motion far faster than a pixel of travel,
     * so most events land on the position the icon already has. Repainting the
     * dock canvas for each of those is the cost of this gesture and none of its
     * value — the same guard deskicon_drag_motion() keeps for the same reason. */
    if ((int)lround(ix) == (int)lround(s->dock_drag.icon_x) &&
        (int)lround(iy) == (int)lround(s->dock_drag.icon_y))
        return;

    s->dock_drag.icon_x = ix;
    s->dock_drag.icon_y = iy;

    /* The cell the icon's CENTRE is over, so a swap happens when the two icons
     * are half past each other rather than a full cell apart. Measured on the
     * icon, not on the cursor: the cursor is wherever in the icon it was pressed
     * and would put the swap point somewhere different for every grab. Relative
     * to the body's origin, which on the run axis is the canvas's — the headroom
     * only ever insets the CROSS axis, and saying so is cheaper than a reader
     * having to re-derive it. */
    double centre = (m.vertical ? iy - m.by : ix - m.bx) + m.icon / 2.0;
    s->dock_drag.slot = dock_slot_at(s, (int)lround(centre),
                                     s->dock_entry_count);

    /* Every mirror, not just this output's: the entry model is server-global, so
     * the shuffle has to show on all of them or two monitors disagree about what
     * order the dock is in for the length of the gesture. */
    dock_relayout(s);
    syn_output_t *out;
    wl_list_for_each(out, &s->outputs, link)
        if (out->wlr_output) wlr_output_schedule_frame(out->wlr_output);
}

void dock_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->dock_drag.active) return;
    syn_output_t *o = s->dock_drag.output;
    if (!o || !o->dock.tree) return;

    if (!s->dock_drag.moved) {
        if (hypot(lx - s->dock_drag.start_x, ly - s->dock_drag.start_y)
                < DOCK_DRAG_THRESHOLD)
            return;
        s->dock_drag.moved = 1;
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
    }

    if (s->dock_drag.icon >= 0) { dock_icon_drag_motion(s, o, lx, ly); return; }
    if (s->dock_drag.icon == DOCK_DRAG_CLOCK) {
        dock_clock_drag_motion(s, o, lx, ly);
        return;
    }

    int bx, by, bw, bh;
    if (!dock_geometry(o, &bx, &by, &bw, &bh)) return;
    s->dock_drag.float_x = lx - bw / 2.0;
    s->dock_drag.float_y = ly - bh / 2.0;
    dock_apply_position(o);
    wlr_output_schedule_frame(o->wlr_output);
}

/* Nearest screen edge to (lx,ly) within output box ob. */
static syn_dock_edge_t nearest_edge(struct wlr_box *ob, double lx, double ly)
{
    double dl = lx - ob->x;
    double dr = (ob->x + ob->width) - lx;
    double dt = ly - ob->y;
    double db = (ob->y + ob->height) - ly;
    double m = dl;
    syn_dock_edge_t edge = SYN_DOCK_EDGE_LEFT;
    if (dr < m) { m = dr; edge = SYN_DOCK_EDGE_RIGHT; }
    if (dt < m) { m = dt; edge = SYN_DOCK_EDGE_TOP; }
    if (db < m) { m = db; edge = SYN_DOCK_EDGE_BOTTOM; }
    return edge;
}

/*
 * Commit a rearrange: put `app_id` at display position `slot` in the PIN LIST,
 * which is the only order that survives a logout.
 *
 * dock_rebuild() lays the pinned entries out first, in config order, and appends
 * the running-only ones after them — so a display position under pin_count is a
 * pin position and one at or past it is not a position at all, just wherever the
 * app happened to map. That asymmetry is the whole of the rule:
 *
 *   - A pinned icon moved anywhere lands in the pin list, clamped to it. Drag it
 *     into the running run and it goes to the end of the pins, which is the
 *     nearest place it can actually be.
 *   - An UNPINNED icon dropped among the pins gets pinned there. That is the
 *     macOS gesture — drag a running app into the dock to keep it — and it is
 *     the one that makes dragging a running icon do something rather than
 *     silently snap back. Dropped among the other running apps it stays
 *     unpinned: there is no order there to write down.
 *
 * Returns true if anything changed.
 */
static bool dock_commit_reorder(syn_server_t *s, const char *app_id, int slot)
{
    syn_config_t *c = &s->config;

    int from = -1;
    for (int i = 0; i < c->dock_pin_count; i++)
        if (strcmp(c->dock_pin[i], app_id) == 0) { from = i; break; }

    if (from < 0) {
        /* Not pinned. Only a drop INSIDE the pinned run means anything. */
        if (slot >= c->dock_pin_count) return false;
        if (c->dock_pin_count >= DOCK_PIN_MAX) {
            wlr_log(WLR_ERROR, "synui: dock: pin list full (max %d)", DOCK_PIN_MAX);
            return false;
        }
        if (slot < 0) slot = 0;
        for (int i = c->dock_pin_count; i > slot; i--)
            memcpy(c->dock_pin[i], c->dock_pin[i - 1], 128);
        snprintf(c->dock_pin[slot], 128, "%s", app_id);
        c->dock_pin_count++;
        return true;
    }

    int to = slot;
    if (to < 0) to = 0;
    if (to >= c->dock_pin_count) to = c->dock_pin_count - 1;
    if (to == from) return false;

    char moving[128];
    snprintf(moving, sizeof(moving), "%s", c->dock_pin[from]);
    if (to < from)
        for (int i = from; i > to; i--) memcpy(c->dock_pin[i], c->dock_pin[i - 1], 128);
    else
        for (int i = from; i < to; i++) memcpy(c->dock_pin[i], c->dock_pin[i + 1], 128);
    snprintf(c->dock_pin[to], 128, "%s", moving);
    return true;
}

/* The icon half of the release. A press that never travelled is the click it
 * always was — that is why the press no longer launches anything directly. */
static void dock_icon_drag_end(syn_server_t *s, int idx, int slot,
                               const char *pressed_app, bool moved)
{
    /*
     * Is the entry we armed on still the entry at that index? dock_rebuild()
     * replaces the whole array on any map or unmap, so an app finishing its
     * launch during the gesture can shift everything after it along. Acting on
     * the index anyway would launch — or worse, rearrange — an app the user
     * never pressed. There is no good recovery, so the gesture is simply
     * abandoned: nothing is a much better answer than something wrong.
     */
    if (idx < 0 || idx >= s->dock_entry_count ||
        strcmp(s->dock_entries[idx].app_id, pressed_app) != 0) {
        dock_relayout(s);
        return;
    }

    if (!moved) {
        dock_entry_click(s, &s->dock_entries[idx]);
        return;
    }

    /* Snapshot before the commit rebuilds the array under us. */
    char app_id[128];
    snprintf(app_id, sizeof(app_id), "%s", pressed_app);

    if (dock_commit_reorder(s, app_id, slot)) {
        dock_state_save(s);
        dock_rebuild(s);   /* re-derives the entry order from the new pin list */
    } else {
        dock_relayout(s);  /* nothing changed — just drop the lifted icon back */
    }

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);
}

void dock_drag_end(syn_server_t *s, double lx, double ly)
{
    if (!s->dock_drag.active) return;

    bool moved = s->dock_drag.moved;
    syn_output_t *drag_o = s->dock_drag.output;
    int icon = s->dock_drag.icon, slot = s->dock_drag.slot;
    char pressed[128];
    snprintf(pressed, sizeof(pressed), "%s", s->dock_drag.icon_app);
    s->dock_drag.active = 0;
    s->dock_drag.moved  = 0;
    s->dock_drag.icon   = DOCK_DRAG_BAR;
    s->dock_drag.output = NULL;

    /* Cleared BEFORE the commit, not after: dock_display_order() and
     * dock_apply_position() both read this state, and dock_rebuild() renders
     * every output on its way through. Committing first would repaint the whole
     * dock still holding a drag that has ended. */
    if (icon >= 0) { dock_icon_drag_end(s, icon, slot, pressed, moved); return; }

    if (icon == DOCK_DRAG_CLOCK) {
        if (moved) {
            /*
             * Dropped past the last icon it goes back to -1 rather than being
             * written as `n`. They look the same today and stop being the same
             * the moment an app opens: a stored 5 is the fifth gap, which walks
             * back up the row as apps quit, while -1 is "the end" and stays
             * there. Nobody who drags the clock to the end of the dock means
             * "and follow the fifth icon from now on".
             */
            int n = s->dock_entry_count;
            s->config.dock_clock_slot = (slot < 0 || slot >= n) ? -1 : slot;
            dock_state_save(s);
        }
        dock_relayout(s);
        syn_output_t *o;
        wl_list_for_each(o, &s->outputs, link)
            if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);
        return;
    }

    if (!moved || !drag_o) {
        /* A press with no travel: not a reposition — just settle back. */
        if (drag_o) dock_apply_position(drag_o);
        return;
    }

    struct wlr_box ob;
    output_box_of(s, drag_o, &ob);
    syn_dock_edge_t edge = nearest_edge(&ob, lx, ly);

    if (edge != s->config.dock_edge) {
        s->config.dock_edge = edge;
        dock_state_save(s);
    }

    /* Land it shown on the new edge, then re-render every mirror in the new
     * orientation. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
        o->dock.unhover_since = 0.0;
        o->dock.last_tick = 0.0;
    }
    dock_relayout(s);
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);
}

/* ── Pinning + persistence ───────────────────────────────── */

/* Resolve ~/.config/synui/dock.state; false if $HOME is unset. */
static bool dock_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "dock.state");
}

void dock_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!dock_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted state — synuirc stands */

    /* The file, when present, is authoritative for both edge and the pin
     * set, so start the pin list empty and refill from `pin=` lines. */
    cfg->dock_pin_count = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;

        if (strncmp(p, "edge=", 5) == 0) {
            const char *v = p + 5;
            if      (strcmp(v, "bottom") == 0) cfg->dock_edge = SYN_DOCK_EDGE_BOTTOM;
            else if (strcmp(v, "top")    == 0) cfg->dock_edge = SYN_DOCK_EDGE_TOP;
            else if (strcmp(v, "left")   == 0) cfg->dock_edge = SYN_DOCK_EDGE_LEFT;
            else if (strcmp(v, "right")  == 0) cfg->dock_edge = SYN_DOCK_EDGE_RIGHT;
        } else if (strncmp(p, "autohide=", 9) == 0) {
            /* Only overrides synuirc when the line is present, so an older
             * dock.state written before this key leaves the config value alone.
             * The same is true of every key below it, which is the whole reason
             * they are matched rather than positional: a dock.state written by
             * an older synui simply carries fewer opinions. */
            cfg->dock_autohide = strcmp(p + 9, "on") == 0;
        } else if (strncmp(p, "ontop=", 6) == 0) {
            cfg->dock_on_top = strcmp(p + 6, "on") == 0;
        } else if (strncmp(p, "magnify=", 8) == 0) {
            cfg->dock_magnify = strcmp(p + 8, "on") == 0;
        } else if (strncmp(p, "clock=", 6) == 0) {
            cfg->dock_clock = strcmp(p + 6, "on") == 0;
        } else if (strncmp(p, "clock_slot=", 11) == 0) {
            int v = atoi(p + 11);
            /* Negative is "past the last icon" — see dock_clock_slot(). The
             * upper end is clamped at layout time against the icons that exist,
             * not here: this file is read before there are any. */
            if (v < 0) v = -1;
            if (v > DOCK_MAX_ENTRIES) v = DOCK_MAX_ENTRIES;
            cfg->dock_clock_slot = v;
        } else if (strncmp(p, "clock_analog=", 13) == 0) {
            cfg->dock_clock_analog = strcmp(p + 13, "on") == 0;
        } else if (strncmp(p, "apps=", 5) == 0) {
            cfg->dock_apps_button = strcmp(p + 5, "on") == 0;
        } else if (strncmp(p, "power=", 6) == 0) {
            cfg->dock_power_button = strcmp(p + 6, "on") == 0;
        } else if (strncmp(p, "pin=", 4) == 0) {
            const char *v = p + 4;
            if (*v && cfg->dock_pin_count < DOCK_PIN_MAX) {
                snprintf(cfg->dock_pin[cfg->dock_pin_count], 128, "%s", v);
                cfg->dock_pin_count++;
            }
        }
    }
    fclose(f);
}

void dock_state_save(syn_server_t *s)
{
    char path[256];
    if (!dock_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: dock: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    static const char *edge_name[] = { "bottom", "top", "left", "right" };
    fprintf(f, "edge=%s\n", edge_name[s->config.dock_edge]);
    fprintf(f, "autohide=%s\n", s->config.dock_autohide ? "on" : "off");
    fprintf(f, "ontop=%s\n",    s->config.dock_on_top   ? "on" : "off");
    fprintf(f, "magnify=%s\n",  s->config.dock_magnify  ? "on" : "off");
    fprintf(f, "clock=%s\n",    s->config.dock_clock    ? "on" : "off");
    fprintf(f, "clock_slot=%d\n", s->config.dock_clock_slot);
    fprintf(f, "clock_analog=%s\n",
            s->config.dock_clock_analog ? "on" : "off");
    fprintf(f, "apps=%s\n",     s->config.dock_apps_button ? "on" : "off");
    fprintf(f, "power=%s\n",    s->config.dock_power_button ? "on" : "off");
    for (int i = 0; i < s->config.dock_pin_count; i++)
        fprintf(f, "pin=%s\n", s->config.dock_pin[i]);
    fclose(f);
}

void dock_pin_toggle(syn_server_t *s, const char *app_id)
{
    if (!app_id || !*app_id) return;
    syn_config_t *c = &s->config;

    int found = -1;
    for (int i = 0; i < c->dock_pin_count; i++)
        if (strcmp(c->dock_pin[i], app_id) == 0) { found = i; break; }

    if (found >= 0) {
        for (int i = found; i < c->dock_pin_count - 1; i++)
            memcpy(c->dock_pin[i], c->dock_pin[i + 1], 128);
        c->dock_pin_count--;
    } else if (c->dock_pin_count < DOCK_PIN_MAX) {
        snprintf(c->dock_pin[c->dock_pin_count], 128, "%s", app_id);
        c->dock_pin_count++;
    } else {
        wlr_log(WLR_ERROR, "synui: dock: pin list full (max %d)", DOCK_PIN_MAX);
        return;
    }

    dock_state_save(s);
    dock_rebuild(s);
}

/* ── Right-click context menu ──────────────────────────────
 *
 * TWO menus in one, and the reason they are one menu is reach.
 *
 * The dock's settings had no pointer route at all: auto-hide lived on the
 * control panel and nowhere else, while the BAR — the other piece of shell
 * furniture on the same desktop — puts every one of its switches on a
 * right-click. So the two halves of the desktop disagreed about how you change
 * them, and the dock's half was the one you had to know a keybinding for.
 *
 * They could not go on a bar-body-only menu, which was the obvious shape. The
 * body a right-click can actually land on is the 8px of padding between icons
 * and whatever is left past the last one; on a full dock that is a target you
 * hunt for. So the settings are on EVERY dock right-click, with the app rows
 * above them when the click landed on an icon. Same rule the desktop menu
 * follows: what you clicked first, what you clicked it on second.
 */

#define DOCKMENU_ITEM_H 30
#define DOCKMENU_SEP_H  9      /* separator rows are shorter than items */
#define DOCKMENU_W      210    /* fits "Windows cover the dock" at 14px */

static bool dockact_is_sep(syn_dockact_t a) { return a == SYN_DOCKACT_SEP; }

static int dockmenu_row_h(syn_server_t *s, int i)
{
    return dockact_is_sep(s->dockmenu.actions[i]) ? DOCKMENU_SEP_H
                                                  : DOCKMENU_ITEM_H;
}

int dockmenu_row_top(syn_server_t *s, int i)
{
    int top = 4;
    for (int k = 0; k < i; k++) top += dockmenu_row_h(s, k);
    return top;
}

int dockmenu_row_height(syn_server_t *s, int i) { return dockmenu_row_h(s, i); }

/* The switches draw a checkmark in the state they are already in, so a row reads
 * as a setting rather than as an action that might do it twice — the same
 * convention deskmenu_row_checked() established. */
bool dockmenu_row_checked(syn_server_t *s, int i)
{
    if (i < 0 || i >= s->dockmenu.action_count) return false;
    switch (s->dockmenu.actions[i]) {
    case SYN_DOCKACT_AUTOHIDE: return s->config.dock_autohide;
    case SYN_DOCKACT_ONTOP:    return s->config.dock_on_top;
    case SYN_DOCKACT_MAGNIFY:  return s->config.dock_magnify;
    case SYN_DOCKACT_CLOCK:    return s->config.dock_clock;
    case SYN_DOCKACT_CLOCK_ANALOG: return s->config.dock_clock_analog;
    case SYN_DOCKACT_APPS:     return s->config.dock_apps_button;
    case SYN_DOCKACT_POWER:    return s->config.dock_power_button;
    default:                   return false;
    }
}

/* Size the popup from the rows already in the array, put it at the cursor and
 * show it. Shared by both menus, so the app menu and the power menu cannot come
 * to sit in different places or clamp to the screen differently. */
static void dockmenu_place(syn_server_t *s, double lx, double ly)
{
    int n = s->dockmenu.action_count;
    int w = DOCKMENU_W, h = 8;
    for (int i = 0; i < n; i++) h += dockmenu_row_h(s, i);

    /* Position above/left of the cursor so a bottom dock's menu pops upward,
     * then clamp within the output under the cursor. */
    int x = (int)lx, y = (int)ly - h;
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, lx, ly);
    if (wo && wo->data) {
        struct wlr_box ob;
        output_box_of(s, (syn_output_t *)wo->data, &ob);
        if (x + w > ob.x + ob.width) x = ob.x + ob.width - w;
        if (y < ob.y) y = ob.y;
        if (x < ob.x) x = ob.x;
        if (y + h > ob.y + ob.height) y = ob.y + ob.height - h;
    }
    s->dockmenu.x = x; s->dockmenu.y = y; s->dockmenu.w = w; s->dockmenu.h = h;
    s->dockmenu.selected = -1;
    s->dockmenu.visible = 1;
    synui_render_dockmenu(s);
}

/* `e` NULL means the click landed on the bar body rather than on an icon. */
void dockmenu_open(syn_server_t *s, syn_dock_entry_t *e, double lx, double ly)
{
    snprintf(s->dockmenu.app_id, sizeof(s->dockmenu.app_id), "%s",
             e ? e->app_id : "");

    int n = 0;
    if (e) {
        s->dockmenu.actions[n++] = e->pinned ? SYN_DOCKACT_UNPIN
                                             : SYN_DOCKACT_PIN;

        const syn_icon_entry_t *ic = icon_lookup(e->app_id);
        if (ic->exec[0])
            s->dockmenu.actions[n++] = e->running ? SYN_DOCKACT_NEWWIN
                                                  : SYN_DOCKACT_OPEN;
        /* Close-one before quit-all: closing a single window is the common
         * intent, and Quit sits furthest from the cursor so it is hard to hit by
         * accident. */
        if (e->running) {
            s->dockmenu.actions[n++] = SYN_DOCKACT_CLOSEWIN;
            s->dockmenu.actions[n++] = SYN_DOCKACT_QUIT;
        }
        s->dockmenu.actions[n++] = SYN_DOCKACT_SEP;
    }

    s->dockmenu.actions[n++] = SYN_DOCKACT_AUTOHIDE;
    /* Only while the dock is pinned on screen. An auto-hiding dock is always on
     * top (see dock_floats_over_windows), so the row would be a switch with
     * nothing behind it — worse than an absent one, because it would appear to
     * be set and ignored. */
    if (!s->config.dock_autohide)
        s->dockmenu.actions[n++] = SYN_DOCKACT_ONTOP;
    s->dockmenu.actions[n++] = SYN_DOCKACT_MAGNIFY;
    s->dockmenu.actions[n++] = SYN_DOCKACT_CLOCK;
    /* Only with a clock to be a style OF. A switch that changes how something
     * absent is drawn is a row that appears to do nothing, and on a vertical
     * dock — where this row is the fix rather than a preference — that is
     * exactly the wrong impression to leave. */
    if (s->config.dock_clock)
        s->dockmenu.actions[n++] = SYN_DOCKACT_CLOCK_ANALOG;
    s->dockmenu.actions[n++] = SYN_DOCKACT_APPS;
    s->dockmenu.actions[n++] = SYN_DOCKACT_POWER;
    s->dockmenu.actions[n++] = SYN_DOCKACT_SEP;
    s->dockmenu.actions[n++] = SYN_DOCKACT_SETTINGS;
    s->dockmenu.action_count = n;
    dockmenu_place(s, lx, ly);
}

/*
 * The power button's menu. Five rows, in the order they escalate: the two that
 * cost nothing, then sleep, then the two that end the session.
 *
 * Ordered that way on purpose. dockmenu_place() puts the popup ABOVE the cursor
 * for a bottom dock, so the row nearest the pointer — and nearest the button
 * that was just pressed — is the last one in the list. Shut Down is therefore
 * the FURTHEST from where the hand already is, which is the same rule that put
 * Quit All Windows at the bottom of the icon menu.
 *
 * No confirmation dialog, and that is deliberate rather than an omission: the
 * menu is the confirmation. A press on the button commits to nothing, and
 * nothing here fires until a second, aimed press lands on a named row.
 */
void dockmenu_open_power(syn_server_t *s, double lx, double ly)
{
    s->dockmenu.app_id[0] = '\0';   /* not an app menu — no icon behind it */

    int n = 0;
    s->dockmenu.actions[n++] = SYN_DOCKACT_LOCK;
    s->dockmenu.actions[n++] = SYN_DOCKACT_LOGOUT;
    s->dockmenu.actions[n++] = SYN_DOCKACT_SUSPEND;
    s->dockmenu.actions[n++] = SYN_DOCKACT_SEP;
    s->dockmenu.actions[n++] = SYN_DOCKACT_REBOOT;
    s->dockmenu.actions[n++] = SYN_DOCKACT_POWEROFF;
    s->dockmenu.action_count = n;
    dockmenu_place(s, lx, ly);
}

/* Item index under (lx,ly), or -1 if outside the menu or over a separator. */
static int dockmenu_item_at(syn_server_t *s, double lx, double ly)
{
    if (lx < s->dockmenu.x || lx >= s->dockmenu.x + s->dockmenu.w ||
        ly < s->dockmenu.y || ly >= s->dockmenu.y + s->dockmenu.h)
        return -1;

    int rel = (int)(ly - s->dockmenu.y - 4);
    int top = 0;
    for (int i = 0; i < s->dockmenu.action_count; i++) {
        int rh = dockmenu_row_h(s, i);
        if (rel >= top && rel < top + rh)
            return dockact_is_sep(s->dockmenu.actions[i]) ? -1 : i;
        top += rh;
    }
    return -1;
}

void dockmenu_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->dockmenu.visible) return;
    int idx = dockmenu_item_at(s, lx, ly);
    if (idx != s->dockmenu.selected) {
        s->dockmenu.selected = idx;
        synui_render_dockmenu(s);
    }
}

void dockmenu_close(syn_server_t *s)
{
    if (!s->dockmenu.visible) return;
    s->dockmenu.visible = 0;
    synui_render_dockmenu(s);
}

/* One switch, flipped, persisted and applied. Every one of these lives in
 * dock.state next to the edge and the pins, so a dock configured by pointer
 * comes back the way it was left — which the auto-hide row already did and is
 * the standard the three new ones are held to. */
static void dockmenu_toggle(syn_server_t *s, int *flag)
{
    *flag = !*flag;
    dock_state_save(s);
    dock_relayout(s);  /* every one of these changes the canvas: repaint all
                        * mirrors, not just the one the menu was opened on */
    dock_wake(s);      /* and act on it this frame, not the next stray one */
}

void dockmenu_click(syn_server_t *s, double lx, double ly)
{
    if (!s->dockmenu.visible) return;
    int idx = dockmenu_item_at(s, lx, ly);
    if (idx < 0) { dockmenu_close(s); return; }   /* click outside → dismiss */

    syn_dockact_t act = s->dockmenu.actions[idx];
    char app_id[128];
    snprintf(app_id, sizeof(app_id), "%s", s->dockmenu.app_id);
    dockmenu_close(s);

    switch (act) {
    case SYN_DOCKACT_PIN:
    case SYN_DOCKACT_UNPIN:
        dock_pin_toggle(s, app_id);
        break;
    case SYN_DOCKACT_OPEN:
    case SYN_DOCKACT_NEWWIN: {
        const syn_icon_entry_t *ic = icon_lookup(app_id);
        if (ic->exec[0]) synui_spawn(ic->exec);
        break;
    }
    case SYN_DOCKACT_CLOSEWIN: {
        /* One window, not the app. Prefer the focused window when it belongs to
         * this app_id — that is the one the user is looking at — and otherwise
         * take the first mapped window we find. Re-resolved from app_id rather
         * than a view pointer stashed at open time, because a window can close
         * on its own while the menu is up. */
        syn_view_t *target = NULL, *f = s->focused_view;
        if (f && f->mapped) {
            const char *aid = view_app_id(f);
            if (aid && strcmp(aid, app_id) == 0) target = f;
        }
        for (int wi = 0; wi < WORKSPACE_MAX && !target; wi++) {
            syn_view_t *v;
            wl_list_for_each(v, &s->workspaces[wi].windows, link) {
                if (!v->mapped) continue;
                const char *aid = view_app_id(v);
                if (aid && strcmp(aid, app_id) == 0) { target = v; break; }
            }
        }
        if (target) view_close(target);
        break;
    }
    case SYN_DOCKACT_QUIT:
        for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
            syn_view_t *v, *tmp;
            wl_list_for_each_safe(v, tmp, &s->workspaces[wi].windows, link) {
                if (!v->mapped) continue;
                const char *aid = view_app_id(v);
                if (aid && strcmp(aid, app_id) == 0) view_close(v);
            }
        }
        break;

    case SYN_DOCKACT_AUTOHIDE:
        dockmenu_toggle(s, &s->config.dock_autohide);
        break;
    case SYN_DOCKACT_ONTOP:
        dockmenu_toggle(s, &s->config.dock_on_top);
        break;
    case SYN_DOCKACT_MAGNIFY:
        dockmenu_toggle(s, &s->config.dock_magnify);
        break;
    case SYN_DOCKACT_APPS:
        dockmenu_toggle(s, &s->config.dock_apps_button);
        break;
    case SYN_DOCKACT_POWER:
        dockmenu_toggle(s, &s->config.dock_power_button);
        break;
    case SYN_DOCKACT_CLOCK:
        dockmenu_toggle(s, &s->config.dock_clock);
        break;
    case SYN_DOCKACT_CLOCK_ANALOG:
        dockmenu_toggle(s, &s->config.dock_clock_analog);
        break;
    case SYN_DOCKACT_SETTINGS:
        /* The category, not the bare panel: the rest of the dock's settings —
         * edge, size, style, opacity, corners — are rows on Desktop, and landing
         * anywhere else would make this the one menu entry that does not take
         * you to what it names. */
        synui_binding_execute(s, "control", "desktop");
        break;

    /*
     * ── The power menu ──────────────────────────────────────────────────────
     *
     * Lock and Log Out go through synui_binding_execute() rather than doing the
     * work here, because both are already actions with a keybinding, a control
     * panel row and a start-menu entry pointing at them, and a fourth caller
     * that reimplements either is a fourth thing to keep in step. The lock in
     * particular is idempotent for exactly this reason — see the `lock` branch
     * in input.c.
     */
    case SYN_DOCKACT_LOCK:
        synui_binding_execute(s, "lock", "");
        break;
    case SYN_DOCKACT_LOGOUT:
        synui_binding_execute(s, "quit", "");
        break;
    case SYN_DOCKACT_SUSPEND:
        /* The configured command, not a literal: power_suspend_cmd is what the
         * idle timer and the lid switch already run, and a dock row that slept
         * the machine a different way from the lid would be a second policy. */
        if (s->config.power_suspend_cmd[0])
            synui_spawn(s->config.power_suspend_cmd);
        break;
    /*
     * ⚠ NO `sudo`. logind's CanPowerOff/CanReboot answer "yes" for the user
     * holding the active seat, so systemctl asks polkit and polkit says yes
     * without a password. Prefixing sudo makes it a command that needs a TTY to
     * prompt on — and spawned from the compositor there is no TTY, so it would
     * fail silently and the dock button would appear to do nothing at all.
     */
    case SYN_DOCKACT_REBOOT:
        synui_spawn("systemctl reboot");
        break;
    case SYN_DOCKACT_POWEROFF:
        synui_spawn("systemctl poweroff");
        break;

    case SYN_DOCKACT_SEP:
        break;   /* not selectable; dockmenu_item_at never returns one */
    }
}
