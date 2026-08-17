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

#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PAD  8

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

/* Fully-shown bar rect for this output's mirror on the current edge. The
 * "run" axis (length) grows with the entry count; the cross axis is the fixed
 * dock_height thickness. Shared by the renderer and the auto-hide tick so both
 * agree on where the bar lives. */
static bool dock_geometry(syn_output_t *o, int *bx, int *by,
                          int *bar_w, int *bar_h)
{
    syn_server_t *s = o->server;
    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    int run = n > 0 ? n * icon + (n + 1) * pad : pad * 2;
    int thick = s->config.dock_height;
    syn_dock_edge_t edge = s->config.dock_edge;

    struct wlr_box ob;
    output_box_of(s, o, &ob);
    if (ob.width <= 0 || ob.height <= 0) return false;

    int w, h;
    if (edge_is_vertical(edge)) { w = thick; h = run; }
    else                        { w = run;   h = thick; }

    int x, y;
    switch (edge) {
    case SYN_DOCK_EDGE_TOP:
        x = ob.x + (ob.width - w) / 2; y = ob.y;
        break;
    case SYN_DOCK_EDGE_LEFT:
        x = ob.x; y = ob.y + (ob.height - h) / 2;
        break;
    case SYN_DOCK_EDGE_RIGHT:
        x = ob.x + ob.width - w; y = ob.y + (ob.height - h) / 2;
        break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:
        x = ob.x + (ob.width - w) / 2; y = ob.y + ob.height - h;
        break;
    }
    if (x < ob.x) x = ob.x;   /* longer than the output: clip to origin */
    if (y < ob.y) y = ob.y;

    *bx = x; *by = y; *bar_w = w; *bar_h = h;
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
 */
static double dock_bar_radius(syn_server_t *s, int bar_w, int bar_h)
{
    double r = s->config.dock_radius;
    if (r < 0.0) r = 0.0;
    double cap = (bar_w < bar_h ? bar_w : bar_h) / 2.0;
    return r > cap ? cap : r;
}

/* Top-left of the icon in display position `i`, dock-canvas-local. The layout
 * is a single run of equal cells, so this is the whole of it — and having it in
 * one place is what lets the drag ask "which cell is the pointer over" with the
 * same arithmetic the renderer places cells with. */
static void dock_slot_pos(syn_server_t *s, int i, int bar_w, int bar_h,
                          int *ix, int *iy)
{
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    if (edge_is_vertical(s->config.dock_edge)) {
        *iy = pad + i * (icon + pad);
        *ix = (bar_w - icon) / 2;
    } else {
        *ix = pad + i * (icon + pad);
        *iy = (bar_h - icon) / 2 - 4;   /* room for the dot below */
    }
}

/* Which cell a point on the run axis falls in, clamped to the icons that exist.
 * `run` is canvas-local along the bar's long axis. */
static int dock_slot_at(int run, int count)
{
    if (count <= 0) return 0;
    int cell = DOCK_ICON_SIZE + DOCK_ICON_PAD;
    int i = (run - DOCK_ICON_PAD) / cell;
    if (run < DOCK_ICON_PAD) i = 0;      /* integer division truncates toward 0 */
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    return i;
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
static void dock_apply_position(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    /* Dragging THE BAR: float freely under the cursor, always visible.
     * Deliberately uncropped — a drag is how the dock is moved between edges and
     * outputs, so it has to be able to cross them.
     *
     * An ICON drag (icon >= 0) is not this. The bar stays exactly where it is:
     * floating it would carry the row of icons the gesture is rearranging along
     * with the cursor, and there would be nothing for the dragged icon to move
     * relative to. */
    if (s->dock_drag.active && s->dock_drag.moved && s->dock_drag.icon < 0 &&
        s->dock_drag.output == o) {
        int dw, dh, dbx, dby;
        if (dock_geometry(o, &dbx, &dby, &dw, &dh))
            dock_set_crop(o, 0, 0, dw, dh, dw, dh);
        wlr_scene_node_set_position(&o->dock.tree->node,
                                    (int)s->dock_drag.float_x,
                                    (int)s->dock_drag.float_y);
        wlr_scene_node_set_enabled(&o->dock.tree->node, true);
        wlr_scene_node_raise_to_top(&o->dock.tree->node);
        return;
    }

    int bx, by, bw, bh;
    if (!s->config.dock_enabled || !dock_geometry(o, &bx, &by, &bw, &bh)) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    double p = o->dock.slide_progress;
    double off = 1.0 - p;
    int x = bx, y = by;
    switch (s->config.dock_edge) {
    case SYN_DOCK_EDGE_TOP:    y = by - (int)lround(off * bh); break;
    case SYN_DOCK_EDGE_LEFT:   x = bx - (int)lround(off * bw); break;
    case SYN_DOCK_EDGE_RIGHT:  x = bx + (int)lround(off * bw); break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:                   y = by + (int)lround(off * bh); break;
    }

    /* Clip the slid rect to this output and paint only that part. */
    struct wlr_box ob;
    output_box_of(s, o, &ob);
    int cx0 = x > ob.x ? x : ob.x;
    int cy0 = y > ob.y ? y : ob.y;
    int cx1 = (x + bw) < (ob.x + ob.width)  ? (x + bw) : (ob.x + ob.width);
    int cy1 = (y + bh) < (ob.y + ob.height) ? (y + bh) : (ob.y + ob.height);
    int cw = cx1 - cx0, ch = cy1 - cy0;

    bool visible = p > 0.001 && cw > 0 && ch > 0;
    if (visible) {
        dock_set_crop(o, cx0 - x, cy0 - y, cw, ch, bw, bh);
        /* The crop just changed the buffer's painted size and offset, and the
         * blur companion is sized and placed FROM those — so it has to be
         * re-synced here and not only after a render, or the frosted patch keeps
         * the pose it had before the slide and hangs off the edge for the whole
         * animation. Same class of staleness as the window blur that kept its
         * old size across a resize; see blur_sync_geometry() in anim.c. */
        syn_buffer_backdrop_blur(o->dock.icons_buf,
                                 dock_style_is_glass(&s->config) && s->config.blur,
                                 (int)lround(dock_bar_radius(s, bw, bh)));
    }
    wlr_scene_node_set_position(&o->dock.tree->node, x, y);
    wlr_scene_node_set_enabled(&o->dock.tree->node, visible);
    if (visible) {
        /* The dock's tree is a UI sibling of window_tree, so raising it to top
         * floats it over everything — including a fullscreen window, which only
         * raises within window_tree. An always-visible dock must not sit on top
         * of a fullscreen game/video: when this output shows one, tuck the dock
         * just below window_tree so the fullscreen view covers it. Otherwise
         * keep it above ordinary windows. */
        if (dock_output_fullscreen(o))
            wlr_scene_node_place_below(&o->dock.tree->node, &s->window_tree->node);
        else
            wlr_scene_node_raise_to_top(&o->dock.tree->node);
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
static void dock_draw_icon(cairo_t *cr, const char *app_id,
                           double ix, double iy, int icon, double scale)
{
    cairo_save(cr);
    if (scale != 1.0) {
        double cx = ix + icon / 2.0, cy = iy + icon / 2.0;
        cairo_translate(cr, cx, cy);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, -cx, -cy);
    }

    const syn_icon_entry_t *ic = icon_lookup(app_id);
    if (ic->icon_surface) {
        double sw = cairo_image_surface_get_width(ic->icon_surface);
        double sh = cairo_image_surface_get_height(ic->icon_surface);
        if (sw > 0 && sh > 0) {
            cairo_save(cr);
            cairo_translate(cr, ix, iy);
            cairo_scale(cr, icon / sw, icon / sh);
            cairo_set_source_surface(cr, ic->icon_surface, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    } else {
        icon_draw_monogram(cr, app_id, ix, iy, icon);
    }
    cairo_restore(cr);
}

static void dock_render_output(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    if (!s->config.dock_enabled) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    int bx, by, bar_w, bar_h;
    if (!dock_geometry(o, &bx, &by, &bar_w, &bar_h)) return;
    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE;
    bool vertical = edge_is_vertical(s->config.dock_edge);

    bool glass  = dock_style_is_glass(&s->config);
    double radius = dock_bar_radius(s, bar_w, bar_h);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(bar_w, bar_h, &cr);
    if (!buf) return;
    cairo_begin(cr);

    dock_paint_body(s, cr, bar_w, bar_h, radius, glass);

    /* Is an icon on THIS server being dragged, and to where. The dragged entry
     * is drawn last and elsewhere, so the loop below skips its cell. */
    bool dragging_icon = s->dock_drag.active && s->dock_drag.moved &&
                         s->dock_drag.icon >= 0 && s->dock_drag.icon < n;

    int order[DOCK_MAX_ENTRIES];
    int ndisp = dock_display_order(s, order, DOCK_MAX_ENTRIES);

    double now = dock_now();
    for (int slot = 0; slot < ndisp; slot++) {
        int i = order[slot];
        syn_dock_entry_t *e = &s->dock_entries[i];
        int ix, iy;
        dock_slot_pos(s, slot, bar_w, bar_h, &ix, &iy);

        /* The hit-box goes on the CELL, not on where the icon is painted: the
         * dragged icon is painted under the cursor, and a hit-box that followed
         * it would leave the gap it came out of clickable and the icon itself
         * hit-testable in mid-air. Nothing hit-tests during a drag anyway, and
         * on release the box is right for wherever the icon settled. */
        e->x = ix; e->y = iy; e->w = icon; e->h = icon;

        if (dragging_icon && i == s->dock_drag.icon) continue;   /* drawn below */

        /* Press-pop: scale the icon about its centre. Only the icon glyph is
         * transformed — the hit-box and running-dot stay put. */
        dock_draw_icon(cr, e->app_id, ix, iy, icon, dock_click_scale(e, now));

        if (e->running) {
            double dx, dy;
            if (vertical) {
                /* Dot on the inner long edge of the column. */
                dx = (s->config.dock_edge == SYN_DOCK_EDGE_LEFT)
                         ? bar_w - 5.0 : 5.0;
                dy = iy + icon / 2.0;
            } else {
                dx = ix + icon / 2.0;
                dy = iy + icon + 6;
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
            cairo_arc(cr, dx, dy, 2.5, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    /* The lifted icon, last so it is over everything, and slightly larger with a
     * shadow under it. Both say "this one is off the surface" — without them a
     * dragged icon and a settled one are the same picture in different places,
     * and which one the pointer is carrying stops being obvious the moment it
     * lines up with a gap. */
    if (dragging_icon) {
        syn_dock_entry_t *e = &s->dock_entries[s->dock_drag.icon];
        double ix = s->dock_drag.icon_x, iy = s->dock_drag.icon_y;
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
        cairo_arc(cr, ix + icon / 2.0, iy + icon * 0.86, icon * 0.40, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
        dock_draw_icon(cr, e->app_id, ix, iy, icon, 1.12);
    }

    cairo_destroy(cr);
    set_scene_buffer(&o->dock.icons_buf, o->dock.tree, buf);

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

void dock_init(syn_server_t *s)
{
    s->dock_entry_count = 0;
    /* -1 is "no icon", and the server struct is zeroed — which would read as
     * "entry 0". Nothing looks at it while the drag is inactive, but a field
     * whose resting value is a valid index is one refactor from being read. */
    s->dock_drag.icon = -1;
    dock_rebuild(s);   /* seeds pinned-only entries; nothing mapped yet */
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
        if (clicking) dock_render_output(o);
        return clicking;
    }

    int bx, by, bw, bh;
    if (!dock_geometry(o, &bx, &by, &bw, &bh)) return false;
    struct wlr_box ob;
    output_box_of(s, o, &ob);

    double cx = s->cursor->x, cy = s->cursor->y;
    int margin = s->config.dock_hover_margin;
    if (margin < 1) margin = 1;
    int pad = DOCK_ICON_PAD;
    syn_dock_edge_t edge = s->config.dock_edge;

    bool on_output = cx >= ob.x && cx < ob.x + ob.width &&
                     cy >= ob.y && cy < ob.y + ob.height;

    /* Reveal trigger: a `margin`-thick strip along the dock's edge, within
     * the bar's footprint (plus a little padding on the long axis). */
    bool in_trigger = false;
    if (on_output) {
        switch (edge) {
        case SYN_DOCK_EDGE_TOP:
            in_trigger = cy < ob.y + margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        case SYN_DOCK_EDGE_LEFT:
            in_trigger = cx < ob.x + margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_RIGHT:
            in_trigger = cx >= ob.x + ob.width - margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_BOTTOM:
        default:
            in_trigger = cy >= ob.y + ob.height - margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        }
    }
    /* Keep-shown region: anywhere over the bar, but only once some of it is
     * actually on screen. dock_geometry() reports the *fully-shown* rect, so
     * testing it unconditionally would treat the whole dock_height band as a
     * reveal trigger and make `margin` meaningless. */
    bool on_screen = o->dock.shown || o->dock.slide_progress > 0.0;
    bool in_bar = on_screen && on_output &&
                  cx >= bx && cx < bx + bw &&
                  cy >= by && cy < by + bh;

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
    if (clicking && on_screen)
        dock_render_output(o);

    bool animating = o->dock.slide_progress != goal;
    bool waiting_to_hide = !engaged && o->dock.shown;
    /* Mid-dwell nothing is moving, but the cursor may have stopped dead in the
     * strip — dock_pointer_motion() won't wake us again, so keep frames coming
     * until the dwell elapses or the cursor leaves. */
    bool waiting_to_show = in_trigger && !engaged;
    return animating || waiting_to_hide || waiting_to_show || clicking;
}

/* Pointer moved: wake the outputs whose dock might need to react (cursor near
 * the dock's edge, a dock already on-screen, or an active drag). dock_tick
 * does the actual state work on the frame this schedules. */
void dock_pointer_motion(syn_server_t *s)
{
    if (!s->config.dock_enabled) return;

    double cx = s->cursor->x, cy = s->cursor->y;
    int band = s->config.dock_height + 8;
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

        /* Entry hit-boxes are dock-canvas-local (identical on every output's
         * mirror); the tree's scene position is that canvas's layout-
         * coordinate origin (which already reflects any slide/float). */
        double rx = lx - o->dock.tree->node.x;
        double ry = ly - o->dock.tree->node.y;

        for (int i = 0; i < s->dock_entry_count; i++) {
            syn_dock_entry_t *e = &s->dock_entries[i];
            if (rx >= e->x && rx < e->x + e->w &&
                ry >= e->y && ry < e->y + e->h)
                return e;
        }
    }
    return NULL;
}

bool dock_bar_at(syn_server_t *s, double lx, double ly, syn_output_t **out)
{
    if (!s->config.dock_enabled) return false;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        int bx, by, bw, bh;
        if (!dock_geometry(o, &bx, &by, &bw, &bh)) continue;
        /* Use the tree's live position (slide/float already applied). */
        double ox = o->dock.tree->node.x, oy = o->dock.tree->node.y;
        if (lx >= ox && lx < ox + bw && ly >= oy && ly < oy + bh) {
            if (out) *out = o;
            return true;
        }
    }
    return false;
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
    s->dock_drag.icon    = -1;        /* the bar, not an icon */
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

    syn_output_t *o = NULL;
    if (!dock_bar_at(s, lx, ly, &o) || !o) return;

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
     * same point of itself instead of jumping its centre to the cursor. */
    s->dock_drag.grab_dx = lx - o->dock.tree->node.x - e->x;
    s->dock_drag.grab_dy = ly - o->dock.tree->node.y - e->y;
    s->dock_drag.icon_x  = e->x;
    s->dock_drag.icon_y  = e->y;
}

/* The lifted icon follows the cursor, and the run-axis position of its centre
 * says which cell it wants. Clamped to the bar: this gesture rearranges, it does
 * not remove — dragging an icon off the dock would need somewhere for it to go
 * and a way to say so, and the right-click menu already unpins. */
static void dock_icon_drag_motion(syn_server_t *s, syn_output_t *o,
                                  double lx, double ly)
{
    int bx, by, bw, bh;
    if (!dock_geometry(o, &bx, &by, &bw, &bh)) return;

    double ix = lx - o->dock.tree->node.x - s->dock_drag.grab_dx;
    double iy = ly - o->dock.tree->node.y - s->dock_drag.grab_dy;

    double max_x = bw - DOCK_ICON_PAD - DOCK_ICON_SIZE;
    double max_y = bh - DOCK_ICON_PAD - DOCK_ICON_SIZE;
    if (ix < DOCK_ICON_PAD) ix = DOCK_ICON_PAD;
    if (iy < DOCK_ICON_PAD) iy = DOCK_ICON_PAD;
    if (ix > max_x) ix = max_x > DOCK_ICON_PAD ? max_x : DOCK_ICON_PAD;
    if (iy > max_y) iy = max_y > DOCK_ICON_PAD ? max_y : DOCK_ICON_PAD;

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
     * and would put the swap point somewhere different for every grab. */
    bool vertical = edge_is_vertical(s->config.dock_edge);
    double centre = (vertical ? iy : ix) + DOCK_ICON_SIZE / 2.0;
    s->dock_drag.slot = dock_slot_at((int)lround(centre), s->dock_entry_count);

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
    s->dock_drag.icon   = -1;
    s->dock_drag.output = NULL;

    /* Cleared BEFORE the commit, not after: dock_display_order() and
     * dock_apply_position() both read this state, and dock_rebuild() renders
     * every output on its way through. Committing first would repaint the whole
     * dock still holding a drag that has ended. */
    if (icon >= 0) { dock_icon_drag_end(s, icon, slot, pressed, moved); return; }

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
             * dock.state written before this key leaves the config value alone. */
            cfg->dock_autohide = strcmp(p + 9, "on") == 0;
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

/* ── Right-click context menu ────────────────────────────── */

#define DOCKMENU_ITEM_H 30
#define DOCKMENU_W      184

void dockmenu_open(syn_server_t *s, syn_dock_entry_t *e, double lx, double ly)
{
    snprintf(s->dockmenu.app_id, sizeof(s->dockmenu.app_id), "%s", e->app_id);

    int n = 0;
    s->dockmenu.actions[n++] = e->pinned ? SYN_DOCKACT_UNPIN : SYN_DOCKACT_PIN;

    const syn_icon_entry_t *ic = icon_lookup(e->app_id);
    if (ic->exec[0])
        s->dockmenu.actions[n++] = e->running ? SYN_DOCKACT_NEWWIN
                                              : SYN_DOCKACT_OPEN;
    /* Close-one before quit-all: closing a single window is the common intent,
     * and Quit sits furthest from the cursor so it is hard to hit by accident. */
    if (e->running) {
        s->dockmenu.actions[n++] = SYN_DOCKACT_CLOSEWIN;
        s->dockmenu.actions[n++] = SYN_DOCKACT_QUIT;
    }
    s->dockmenu.action_count = n;

    int w = DOCKMENU_W, h = n * DOCKMENU_ITEM_H + 8;

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

/* Item index under (lx,ly), or -1 if outside the menu. */
static int dockmenu_item_at(syn_server_t *s, double lx, double ly)
{
    if (lx < s->dockmenu.x || lx >= s->dockmenu.x + s->dockmenu.w ||
        ly < s->dockmenu.y || ly >= s->dockmenu.y + s->dockmenu.h)
        return -1;
    int idx = (int)((ly - s->dockmenu.y - 4) / DOCKMENU_ITEM_H);
    if (idx < 0 || idx >= s->dockmenu.action_count) return -1;
    return idx;
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
    }
}
