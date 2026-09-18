/*
 * vdisplay.c — virtual displays: a screen with no monitor behind it
 *
 * A virtual display is a wlroots headless output added to the running
 * compositor. It is a real output in every way the rest of synui cares about —
 * it is in s->outputs, it takes a workspace, the bar and the dock and the
 * wallpaper come up on it, windows can be moved to it — and the only thing it
 * does not have is a cable. Nothing scans it out; it renders into a buffer,
 * and whoever wants the picture takes it with screencopy or export-dmabuf.
 *
 * That is what it is FOR. `syn-remote stream` serves one to Moonlight, so a
 * remote session gets its own head at the size the client asked for instead of
 * a copy of whatever monitor happens to be plugged in — different resolution,
 * different refresh, its own windows.
 *
 * ── HOW IT ATTACHES ──────────────────────────────────────────────────────────
 *
 * ⛔ THE HEADLESS BACKEND IS CREATED AT STARTUP AND ADDED BEFORE THE MULTI
 * BACKEND STARTS, even when no virtual display is ever asked for. wlroots says
 * a backend should be added to a multi-backend before that backend is started,
 * and synui's is started in synui_main(); creating the headless side later
 * would mean adding a backend to a running multi and depending on it starting
 * the newcomer for us. It costs nothing to have around — a headless backend
 * with no outputs does nothing at all — and it means `vdisplay add` is one
 * call that cannot fail for a structural reason.
 *
 * ⚠ wlr_backend_autocreate() does not promise a multi-backend. It returns one
 * for the ordinary DRM session, but a single-backend return is possible, and
 * adding to it would be a type error rather than a failure — hence the
 * wlr_backend_is_multi() check, and hence `vdisplay add` answering "this synui
 * cannot make one" rather than crashing.
 *
 * ── WHAT THE REST OF THE COMPOSITOR HAD TO LEARN ─────────────────────────────
 *
 * ⛔ A VIRTUAL DISPLAY IS NEVER DPMS-BLANKED BY THE IDLE STAGE (power.c). The
 * whole failure this package's remote-desktop work keeps running into is that a
 * blanked output cannot be captured — screencopy answers "failed to copy
 * output" and the far end gets grey. Every reason to blank a screen is about a
 * panel somebody is not looking at: the backlight, the burn-in, the room at
 * 2am. A headless output has none of those, so the idle stage skips it and the
 * one class of bug that made unattended remote access look broken cannot
 * happen on the head that exists for it.
 *
 * ⛔ AND outputs.conf DOES NOT REMEMBER ONE (output_persist.c). The caller
 * states the geometry in the same breath as asking for the display; a saved
 * entry would silently apply last week's mode over this call's, and the
 * request would be ignored with nothing saying so.
 *
 * ── SOLO ─────────────────────────────────────────────────────────────────────
 *
 * `solo` is the headless-only half: every other output goes dark while one
 * stays lit, so a machine being worked on from somewhere else is not also
 * displaying that session to the room it is sitting in. It is DPMS off, not
 * disabled — a disabled output leaves the layout and takes its windows'
 * positions with it, and the layout reshuffle is visible at the far end as
 * everything jumping one screen to the left.
 *
 * ⛔ SOLO IS INERT WHEN ITS OUTPUT IS NOT THERE. A solo naming an output that
 * has been unplugged — or a virtual display that has been removed — would
 * otherwise blank every screen in the house with no way to get one back from
 * the keyboard. So: the name is checked against s->outputs on every pass, and
 * the output going away clears it.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

/* How many at once. Each one is a full scene render target — a bar, a dock, a
 * wallpaper and a frame timer — so this is a guard against a script in a loop
 * rather than a considered ceiling. */
#define VDISPLAY_MAX 4

/* The window the geometry has to land in. The floor is where a desktop stops
 * being usable at all (the bar alone wants more than 640 wide); the ceiling is
 * 8K, past which the buffers are large enough that a typo would be measured in
 * gigabytes. Refresh is mHz, as wlroots counts it. */
#define VDISPLAY_MIN_W   640
#define VDISPLAY_MIN_H   480
#define VDISPLAY_MAX_W   7680
#define VDISPLAY_MAX_H   4320
#define VDISPLAY_MIN_HZ  24000
#define VDISPLAY_MAX_HZ  360000

void vdisplay_setup(syn_server_t *s)
{
    if (!s->backend) return;

    if (!wlr_backend_is_multi(s->backend)) {
        /* Not an error and not worth a warning at every start: a nested synui
         * on the Wayland backend is a perfectly good compositor that simply
         * cannot grow a head. `vdisplay add` says so when somebody asks. */
        wlr_log(WLR_DEBUG, "synui: vdisplay: backend is not a multi-backend — "
                "virtual displays unavailable in this session");
        return;
    }

    s->vdisp.backend = wlr_headless_backend_create(
        wl_display_get_event_loop(s->display));
    if (!s->vdisp.backend) {
        wlr_log(WLR_ERROR, "synui: vdisplay: headless backend creation failed "
                "— virtual displays unavailable in this session");
        return;
    }

    if (!wlr_multi_backend_add(s->backend, s->vdisp.backend)) {
        wlr_log(WLR_ERROR, "synui: vdisplay: could not add the headless "
                "backend to the session's");
        wlr_backend_destroy(s->vdisp.backend);
        s->vdisp.backend = NULL;
        return;
    }

    wlr_log(WLR_INFO, "synui: vdisplay: headless backend ready (no outputs "
            "until one is asked for)");
}

bool vdisplay_available(syn_server_t *s)
{
    return s && s->vdisp.backend != NULL;
}

/* ⛔ THE FLAG, NOT THE BACKEND. wlr_output_is_headless() answers yes for every
 * output in a session started with WLR_BACKENDS=headless — a nested synui, the
 * test rigs — so `virtual remove all` would take out the screen the compositor
 * is running on, and the idle blank stage would skip a head that IS somebody's
 * only display. What every caller means by "virtual" is "somebody asked for
 * this one", and that is what gets stamped on. */
bool vdisplay_is(syn_output_t *o)
{
    return o && o->virt;
}

void vdisplay_stamp_new(syn_server_t *s, syn_output_t *o)
{
    if (!s || !o) return;
    o->virt = s->vdisp.claiming;
}

int vdisplay_count(syn_server_t *s)
{
    int n = 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (vdisplay_is(o)) n++;
    return n;
}

static syn_output_t *vdisplay_named(syn_server_t *s, const char *name)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (strcmp(o->wlr_output->name, name) == 0) return o;
    return NULL;
}

/* The geometry both `add` and `mode` have to agree about. One function, so a
 * resize cannot accept something a create would have refused. */
static bool vdisplay_geometry_ok(int w, int h, int refresh_mhz, const char **err)
{
    if (w < VDISPLAY_MIN_W || w > VDISPLAY_MAX_W ||
        h < VDISPLAY_MIN_H || h > VDISPLAY_MAX_H) {
        *err = "that size is outside what a virtual display will take";
        return false;
    }
    if (refresh_mhz && (refresh_mhz < VDISPLAY_MIN_HZ ||
                        refresh_mhz > VDISPLAY_MAX_HZ)) {
        *err = "that refresh rate is outside what a virtual display will take";
        return false;
    }
    return true;
}

struct wlr_output *vdisplay_add(syn_server_t *s, int w, int h, int refresh_mhz,
                                double scale, const char **err)
{
    const char *ignored = NULL;
    if (!err) err = &ignored;
    *err = NULL;

    if (!vdisplay_available(s)) {
        *err = "this synui cannot make a virtual display";
        return NULL;
    }
    if (vdisplay_count(s) >= VDISPLAY_MAX) {
        *err = "there are already as many virtual displays as synui will make";
        return NULL;
    }
    if (!vdisplay_geometry_ok(w, h, refresh_mhz, err)) return NULL;
    if (scale <= 0.0) scale = 1.0;
    if (scale < 0.25 || scale > 8.0) {
        *err = "that scale is outside what a virtual display will take";
        return NULL;
    }

    /* ⚠ THE new_output SIGNAL FIRES INSIDE THIS CALL. wlr_headless_add_output()
     * emits it synchronously, so by the time it returns server_new_output() has
     * already built the syn_output_t, placed it in the layout and given it a
     * scene output. Everything below is therefore a CHANGE to a live output,
     * not part of building one — which is why it goes through a commit rather
     * than through the state the output was created with. */
    s->vdisp.claiming = 1;
    struct wlr_output *wlr = wlr_headless_add_output(s->vdisp.backend,
                                                     (unsigned)w, (unsigned)h);
    s->vdisp.claiming = 0;
    if (!wlr) {
        *err = "the headless backend refused the output";
        return NULL;
    }

    /* The display panel and `synctl outputs` show connector names; "HEADLESS-1"
     * says nothing about what it is for. wlroots hands the description to
     * wl_output and xdg_output, so this is what a client's monitor list reads. */
    wlr_output_set_description(wlr, "Virtual display");

    /* The mode the caller asked for, refresh included. wlr_headless_add_output
     * takes a size and nothing else and comes up at the backend's default
     * refresh, so a request for 120Hz is only honoured by this commit — and
     * refresh is not cosmetic here: it is the rate the frame timer runs at,
     * which is the rate anything capturing this head will see. */
    if (refresh_mhz) {
        struct wlr_output_state st;
        wlr_output_state_init(&st);
        wlr_output_state_set_custom_mode(&st, w, h, refresh_mhz);
        if (!wlr_output_commit_state(wlr, &st))
            wlr_log(WLR_ERROR, "synui: vdisplay: %s would not take %dx%d@%d.%03dHz",
                    wlr->name, w, h, refresh_mhz / 1000, refresh_mhz % 1000);
        wlr_output_state_finish(&st);
    }

    if (scale != 1.0) {
        struct wlr_output_state st;
        wlr_output_state_init(&st);
        wlr_output_state_set_scale(&st, (float)scale);
        if (!wlr_output_commit_state(wlr, &st))
            wlr_log(WLR_ERROR, "synui: vdisplay: %s would not take scale %.2f",
                    wlr->name, scale);
        wlr_output_state_finish(&st);
    }

    wlr_log(WLR_INFO, "synui: vdisplay: %s is up — %dx%d@%dHz scale %.2f",
            wlr->name, w, h, refresh_mhz ? refresh_mhz / 1000 : 60, scale);
    return wlr;
}

/* Resize one that is already up.
 *
 * ⛔ THIS IS WHAT MAKES A STREAM THE CLIENT'S SIZE. Sunshine runs its
 * global_prep_cmd before the capture starts and hands it SUNSHINE_CLIENT_WIDTH,
 * _HEIGHT and _FPS, so `syn-remote stream prep` can put the head at exactly
 * what the far end asked for. The alternative is a new display per connection,
 * and that cannot work: the head's name is what sunshine.conf pins as
 * output_name, wlroots hands out HEADLESS-1, -2, -3… in turn, and destroying a
 * head RE-HOMES every window that was on it onto another screen. One display
 * for the life of the session, resized as clients come and go.
 *
 * ⚠ Refusing a resize of a REAL monitor here rather than passing it through:
 * changing a physical mode belongs to the display panel and outputs.conf, and
 * a second path to it would be a second owner of what that file remembers. */
bool vdisplay_mode(syn_server_t *s, const char *name, int w, int h,
                   int refresh_mhz, const char **err)
{
    const char *ignored = NULL;
    if (!err) err = &ignored;
    *err = NULL;

    if (!vdisplay_geometry_ok(w, h, refresh_mhz, err)) return false;

    syn_output_t *o = name ? vdisplay_named(s, name) : NULL;
    if (!o)            { *err = "no such output";                       return false; }
    if (!vdisplay_is(o)) { *err = "that is a monitor, not a virtual display"; return false; }

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_custom_mode(&st, w, h, refresh_mhz);
    bool ok = wlr_output_commit_state(o->wlr_output, &st);
    wlr_output_state_finish(&st);
    if (!ok) { *err = "the backend refused that mode"; return false; }

    /* Everything that measures itself against an output box — the bar, the
     * dock, the tiling — is told by the ordinary path a monitor's mode change
     * takes. */
    output_layout_changed(s);

    wlr_log(WLR_INFO, "synui: vdisplay: %s is now %dx%d@%dHz",
            o->wlr_output->name, w, h,
            refresh_mhz ? refresh_mhz / 1000 : 60);
    return true;
}

int vdisplay_remove(syn_server_t *s, const char *name)
{
    int removed = 0;
    bool all = name == NULL || strcmp(name, "all") == 0;

    /* ⚠ SAFE against the list changing underneath: wlr_output_destroy() runs
     * output_destroy() synchronously, which unlinks this entry. */
    syn_output_t *o, *tmp;
    wl_list_for_each_safe(o, tmp, &s->outputs, link) {
        if (!vdisplay_is(o)) continue;
        if (!all && strcmp(o->wlr_output->name, name) != 0) continue;
        wlr_log(WLR_INFO, "synui: vdisplay: removing %s", o->wlr_output->name);
        wlr_output_destroy(o->wlr_output);
        removed++;
    }
    return removed;
}

/* ── Solo ────────────────────────────────────────────────── */

const char *vdisplay_solo(syn_server_t *s)
{
    return s->vdisp.solo[0] ? s->vdisp.solo : NULL;
}

bool vdisplay_solo_is(syn_server_t *s, struct wlr_output *o)
{
    if (!s->vdisp.solo[0] || !o) return false;
    return strcmp(s->vdisp.solo, o->name) == 0;
}

/* Is the solo name pointing at an output that is actually here?
 *
 * ⛔ THE ONE QUESTION THAT KEEPS THE ROOM'S SCREENS FROM GOING DARK FOREVER.
 * power.c asks this before it blanks anything on solo's account, so a solo
 * naming something that is gone blanks nothing rather than everything. */
bool vdisplay_solo_live(syn_server_t *s)
{
    return s->vdisp.solo[0] && vdisplay_named(s, s->vdisp.solo) != NULL;
}

bool vdisplay_solo_set(syn_server_t *s, const char *name, const char **err)
{
    const char *ignored = NULL;
    if (!err) err = &ignored;
    *err = NULL;

    if (!name || !*name || strcmp(name, "off") == 0) {
        if (!s->vdisp.solo[0]) return true;
        s->vdisp.solo[0] = '\0';
        power_reapply_blank(s);
        wlr_log(WLR_INFO, "synui: vdisplay: solo off — every screen is lit again");
        return true;
    }

    /* ⛔ IT MUST BE HERE NOW. Accepting a name for an output that might turn up
     * later is accepting a state in which everything is dark and the thing that
     * was supposed to be lit does not exist. */
    if (!vdisplay_named(s, name)) {
        *err = "no such output";
        return false;
    }

    snprintf(s->vdisp.solo, sizeof(s->vdisp.solo), "%s", name);
    power_reapply_blank(s);
    wlr_log(WLR_INFO, "synui: vdisplay: solo on %s — every other screen is off",
            s->vdisp.solo);
    return true;
}

/* An output is being destroyed (output_destroy, while it is still in the list).
 *
 * ⛔ CLEAR SOLO IF IT WAS THE ONE. vdisplay_solo_live() already makes the stale
 * case inert, but leaving the name behind means the next `vdisplay add` that
 * happens to get the same HEADLESS-n back silently re-enters solo — a mode
 * nobody asked for this time, and the screens go dark on their own. */
void vdisplay_output_gone(syn_server_t *s, syn_output_t *o)
{
    if (!s->vdisp.solo[0] || !o) return;
    if (strcmp(s->vdisp.solo, o->wlr_output->name) != 0) return;

    wlr_log(WLR_INFO, "synui: vdisplay: %s was solo and is going — solo off",
            s->vdisp.solo);
    s->vdisp.solo[0] = '\0';
    /* ⚠ NOT power_reapply_blank() from here. This runs with the output still in
     * s->outputs and half torn down; the caller re-applies once it is out. */
}
