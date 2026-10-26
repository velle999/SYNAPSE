/*
 * cursor_hide.c — the cursor gets out of the way of a film and a screensaver
 *
 * Two things hide it:
 *
 *   - the SCREENSAVER, a full-screen picture that had an arrow parked on it for
 *     as long as it was up;
 *   - a FULLSCREEN window the pointer has rested over for
 *     `cursor_hide_fullscreen` seconds (3 by default; 0 never) — a film, a
 *     stream, a slideshow, where the arrow otherwise sits on the picture until
 *     somebody nudges the mouse.
 *
 * Motion, a button, the wheel or a pen brings it straight back. A keystroke
 * does not: pausing a film from the keyboard leaves the picture clear.
 *
 * ⚠ HIDING NEVER TOUCHES POINTER FOCUS. The usual way back to a client's own
 * cursor is to clear pointer focus and re-enter, which makes the client set it
 * again — cursor_set_deco() does exactly that over a titlebar. Here it would be
 * a bug: the window being hidden over is fullscreen, which is where games are,
 * and clearing focus deactivates a pointer constraint. A ONESHOT lock is
 * DESTROYED by that, and mouse-look is dead for the rest of the session (the
 * letterbox-bar bug, synui 512). So every image the cursor is given goes
 * through cursor_show_xcursor()/cursor_show_surface(), which remember it, and
 * showing the cursor again re-applies what was remembered. While it is hidden,
 * a client changing its cursor is remembered and not drawn.
 *
 * "Over a fullscreen window" is the bar's own rule, output_fullscreen_view():
 * the monitor under the pointer shows a desktop with a fullscreen window on it.
 * Not while one of synui's panels is open — the pointer is being used there —
 * and the countdown looks again once it closes.
 *
 * The countdown is NOT restarted per event. A mouse reports at up to 1000 Hz
 * and every timer update is a timerfd syscall, so activity only stamps the
 * time; a timer that finds it fired early sleeps the remainder.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "synui.h"

static uint32_t cursor_hide_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── The remembered image ────────────────────────────────── */

static void cursor_forget_surface(syn_server_t *s)
{
    wl_list_remove(&s->cursor_hide.surface_destroy.link);
    wl_list_init(&s->cursor_hide.surface_destroy.link);
    s->cursor_hide.surface = NULL;
}

/* A client's cursor surface went away while it was the remembered image. What
 * comes back then is no image, which is what wlr_cursor itself shows for a
 * destroyed cursor surface. */
static void cursor_surface_destroyed(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, cursor_hide.surface_destroy);
    cursor_forget_surface(s);
}

void cursor_show_xcursor(syn_server_t *s, const char *name)
{
    cursor_forget_surface(s);
    s->cursor_hide.xcursor = name;
    if (!s->cursor_hide.why)
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, name);
}

void cursor_show_surface(syn_server_t *s, struct wlr_surface *surface,
                         int32_t hot_x, int32_t hot_y)
{
    cursor_forget_surface(s);
    s->cursor_hide.xcursor = NULL;
    s->cursor_hide.surface = surface;
    s->cursor_hide.hot_x   = hot_x;
    s->cursor_hide.hot_y   = hot_y;
    if (surface)
        wl_signal_add(&surface->events.destroy, &s->cursor_hide.surface_destroy);
    if (!s->cursor_hide.why)
        wlr_cursor_set_surface(s->cursor, surface, hot_x, hot_y);
}

/* ── Hiding ──────────────────────────────────────────────── */

void cursor_hide(syn_server_t *s, uint32_t why)
{
    uint32_t was = s->cursor_hide.why;
    s->cursor_hide.why |= why;
    if (was) return;
    wlr_cursor_unset_image(s->cursor);
    wlr_log(WLR_DEBUG, "synui: cursor: hidden (%s)",
            why & SYN_CURSOR_HIDE_SAVER ? "screensaver" : "fullscreen");
}

void cursor_unhide(syn_server_t *s, uint32_t why)
{
    if (!(s->cursor_hide.why & why)) return;
    s->cursor_hide.why &= ~why;
    if (s->cursor_hide.why) return;

    if (s->cursor_hide.xcursor)
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, s->cursor_hide.xcursor);
    else
        wlr_cursor_set_surface(s->cursor, s->cursor_hide.surface,
                               s->cursor_hide.hot_x, s->cursor_hide.hot_y);
    wlr_log(WLR_DEBUG, "synui: cursor: shown");
}

/* ── Over a fullscreen window ────────────────────────────── */

static bool cursor_on_fullscreen(syn_server_t *s)
{
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
    return wo && wo->data && output_fullscreen_view(s, wo->data);
}

static void cursor_idle_arm(syn_server_t *s, uint32_t ms)
{
    wl_event_source_timer_update(s->cursor_hide.idle, ms ? (int)ms : 1);
    s->cursor_hide.armed = true;
}

static int cursor_idle_cb(void *data)
{
    syn_server_t *s = data;
    s->cursor_hide.armed = false;

    int secs = s->config.cursor_hide_fullscreen;
    if (secs <= 0 || (s->cursor_hide.why & SYN_CURSOR_HIDE_FULLSCREEN))
        return 0;

    uint32_t want = (uint32_t)secs * 1000;
    uint32_t idle = cursor_hide_now_ms() - s->cursor_hide.last_ms;
    if (idle < want) {
        cursor_idle_arm(s, want - idle);
        return 0;
    }
    if (!cursor_on_fullscreen(s)) return 0;

    /* A panel open over the film: keep the arrow, and look again later — it is
     * most often closed from the keyboard, with no motion to start a count. */
    if (panel_any_visible(s)) {
        cursor_idle_arm(s, want);
        return 0;
    }
    cursor_hide(s, SYN_CURSOR_HIDE_FULLSCREEN);
    return 0;
}

void cursor_pointer_activity(syn_server_t *s)
{
    s->cursor_hide.last_ms = cursor_hide_now_ms();
    cursor_unhide(s, SYN_CURSOR_HIDE_FULLSCREEN);

    int secs = s->config.cursor_hide_fullscreen;
    if (secs > 0 && !s->cursor_hide.armed)
        cursor_idle_arm(s, (uint32_t)secs * 1000);
}

/* Fullscreen came or went, or a monitor changed desktops, under a pointer that
 * need not have moved: a film put fullscreen from the keyboard starts its count
 * now, and leaving one shows the cursor at once instead of on the next nudge. */
void cursor_fullscreen_changed(syn_server_t *s)
{
    if (!cursor_on_fullscreen(s)) {
        cursor_unhide(s, SYN_CURSOR_HIDE_FULLSCREEN);
        return;
    }
    int secs = s->config.cursor_hide_fullscreen;
    if (secs > 0 && !s->cursor_hide.armed &&
        !(s->cursor_hide.why & SYN_CURSOR_HIDE_FULLSCREEN)) {
        s->cursor_hide.last_ms = cursor_hide_now_ms();
        cursor_idle_arm(s, (uint32_t)secs * 1000);
    }
}

/* ── Lifetime ────────────────────────────────────────────── */

void cursor_hide_init(syn_server_t *s)
{
    s->cursor_hide.why     = 0;
    s->cursor_hide.xcursor = "default";
    s->cursor_hide.surface = NULL;
    s->cursor_hide.surface_destroy.notify = cursor_surface_destroyed;
    wl_list_init(&s->cursor_hide.surface_destroy.link);
    s->cursor_hide.idle = wl_event_loop_add_timer(
        wl_display_get_event_loop(s->display), cursor_idle_cb, s);
    s->cursor_hide.armed   = false;
    s->cursor_hide.last_ms = cursor_hide_now_ms();
}

void cursor_hide_finish(syn_server_t *s)
{
    cursor_forget_surface(s);
    if (s->cursor_hide.idle) {
        wl_event_source_remove(s->cursor_hide.idle);
        s->cursor_hide.idle = NULL;
    }
}
