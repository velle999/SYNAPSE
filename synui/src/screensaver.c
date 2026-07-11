/*
 * screensaver.c — org.freedesktop.ScreenSaver, so apps can inhibit idle
 *
 * This is the D-Bus name every desktop app reaches for when it wants the screen
 * to stay on: Firefox and Chrome playing video, mpv, VLC, Steam, most games.
 * Nothing owned it, so every one of those calls failed — the journal logged
 * ~30 "proxy is for the well-known name org.freedesktop.ScreenSaver without an
 * owner" warnings per boot — and the screen would dim on top of a full-screen
 * film.
 *
 * That gap is why synui-media-inhibit exists: a shell script polling pw-dump for
 * a running audio stream and forging a Wayland idle inhibitor when it sees one.
 * That guesses ("is audio playing?") at what an app will now simply tell us
 * ("please don't idle"), and it cannot know *why* or notice a silent video. It
 * stays for now — it still covers apps that inhibit via PipeWire alone — but
 * this is the mechanism that should carry the load.
 *
 * An Inhibit() is held until the caller UnInhibit()s it or drops off the bus.
 * That last part matters: a browser that crashes mid-video would otherwise pin
 * the screen on forever, so we watch NameOwnerChanged and release the cookies of
 * any caller that disappears.
 *
 * The bus fd lives in the Wayland event loop, so this costs nothing when idle
 * and never needs a thread. Losing the bus is not fatal — synui runs fine
 * without it (the ISO's root session may have no session bus at all), it just
 * logs and carries on with the Wayland idle-inhibit protocol alone.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/util/log.h>

#include "synui.h"

/* One live Inhibit(). `owner` is the caller's unique bus name (":1.42"), kept so
 * the cookie can be dropped if that client vanishes without calling UnInhibit. */
typedef struct {
    uint32_t cookie;
    char     owner[64];
    char     app[64];
} syn_ss_inhibit_t;

#define SS_MAX_INHIBITS 32

static struct {
    sd_bus                 *bus;
    struct wl_event_source *src;
    syn_ss_inhibit_t        held[SS_MAX_INHIBITS];
    int                     n;
    uint32_t                next_cookie;
} ss;

/* ── Inhibit bookkeeping ─────────────────────────────────── */

/* Recompute the idle state from both sources and push it to the notifier.
 * Called after anything that could change the count. */
static void ss_sync(syn_server_t *s)
{
    bool inhibited = idle_inhibited(s);
    wlr_idle_notifier_v1_set_inhibited(s->idle_notifier, inhibited);

    /* Re-arm (or disarm) the dim/blank/lock stages, and undo anything that has
     * already fired — the same path a Wayland inhibitor appearing takes. */
    power_notify_activity(s);
}

static void ss_drop_owner(syn_server_t *s, const char *owner)
{
    int before = ss.n;

    for (int i = 0; i < ss.n; ) {
        if (strcmp(ss.held[i].owner, owner) == 0) {
            wlr_log(WLR_INFO, "synui: screensaver: '%s' vanished, releasing "
                    "its inhibit (cookie %u)", ss.held[i].app, ss.held[i].cookie);
            ss.held[i] = ss.held[--ss.n];   /* order does not matter */
        } else {
            i++;
        }
    }

    if (ss.n != before) {
        s->screensaver_inhibitors = ss.n;
        ss_sync(s);
    }
}

/* ── Method handlers ─────────────────────────────────────── */

static int ss_method_inhibit(sd_bus_message *m, void *userdata,
                             sd_bus_error *err)
{
    syn_server_t *s = userdata;
    const char *app = NULL, *reason = NULL;

    int r = sd_bus_message_read(m, "ss", &app, &reason);
    if (r < 0) return r;

    if (ss.n >= SS_MAX_INHIBITS) {
        /* Refuse rather than silently not inhibiting: a caller that thinks it
         * holds an inhibit it does not is worse than one told it failed. */
        sd_bus_error_set_const(err, SD_BUS_ERROR_LIMITS_EXCEEDED,
                               "too many screensaver inhibitors");
        return -EINVAL;
    }

    const char *owner = sd_bus_message_get_sender(m);
    if (!owner) owner = "";

    syn_ss_inhibit_t *h = &ss.held[ss.n++];
    h->cookie = ++ss.next_cookie;
    snprintf(h->owner, sizeof(h->owner), "%s", owner);
    snprintf(h->app,   sizeof(h->app),   "%s", app ? app : "?");

    s->screensaver_inhibitors = ss.n;
    ss_sync(s);

    wlr_log(WLR_INFO, "synui: screensaver: inhibit by '%s' (%s) → cookie %u",
            h->app, reason ? reason : "no reason", h->cookie);

    return sd_bus_reply_method_return(m, "u", h->cookie);
}

static int ss_method_uninhibit(sd_bus_message *m, void *userdata,
                               sd_bus_error *err)
{
    (void)err;
    syn_server_t *s = userdata;
    uint32_t cookie = 0;

    int r = sd_bus_message_read(m, "u", &cookie);
    if (r < 0) return r;

    for (int i = 0; i < ss.n; i++) {
        if (ss.held[i].cookie != cookie) continue;

        wlr_log(WLR_INFO, "synui: screensaver: uninhibit '%s' (cookie %u)",
                ss.held[i].app, cookie);
        ss.held[i] = ss.held[--ss.n];
        s->screensaver_inhibitors = ss.n;
        ss_sync(s);
        break;
    }

    /* An unknown cookie is not an error worth failing the call over — a client
     * double-releasing is common and harmless. */
    return sd_bus_reply_method_return(m, "");
}

/* Apps poll this to ask "is the screensaver showing right now". We report the
 * blank stage, which is the closest thing synui has to one. */
static int ss_method_get_active(sd_bus_message *m, void *userdata,
                                sd_bus_error *err)
{
    (void)err;
    syn_server_t *s = userdata;
    return sd_bus_reply_method_return(m, "b", s->power.blanked ? 1 : 0);
}

/* "The user is here" — same effect as touching the mouse. */
static int ss_method_simulate_activity(sd_bus_message *m, void *userdata,
                                       sd_bus_error *err)
{
    (void)err;
    syn_server_t *s = userdata;
    power_notify_activity(s);
    return sd_bus_reply_method_return(m, "");
}

static int ss_method_lock(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
    (void)err;
    syn_server_t *s = userdata;
    wlr_log(WLR_INFO, "synui: screensaver: Lock() requested over D-Bus");
    synui_spawn(s->config.power_lock_cmd);
    return sd_bus_reply_method_return(m, "");
}

/* A caller dropping off the bus (crash, quit) arrives here as a
 * NameOwnerChanged with an empty new-owner. */
static int ss_name_owner_changed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *err)
{
    (void)err;
    syn_server_t *s = userdata;
    const char *name = NULL, *old_owner = NULL, *new_owner = NULL;

    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) return r;

    if (name && new_owner && !*new_owner)
        ss_drop_owner(s, name);

    return 0;
}

static const sd_bus_vtable ss_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD_WITH_ARGS("Inhibit",
        SD_BUS_ARGS("s", application_name, "s", reason_for_inhibit),
        SD_BUS_RESULT("u", cookie),
        ss_method_inhibit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("UnInhibit",
        SD_BUS_ARGS("u", cookie), SD_BUS_NO_RESULT,
        ss_method_uninhibit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("GetActive",
        SD_BUS_NO_ARGS, SD_BUS_RESULT("b", active),
        ss_method_get_active, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("SimulateUserActivity",
        SD_BUS_NO_ARGS, SD_BUS_NO_RESULT,
        ss_method_simulate_activity, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("Lock",
        SD_BUS_NO_ARGS, SD_BUS_NO_RESULT,
        ss_method_lock, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* ── Event loop integration ──────────────────────────────── */

static int ss_readable(int fd, uint32_t mask, void *data)
{
    (void)fd;
    (void)mask;
    syn_server_t *s = data;

    /* Drain: sd_bus_process handles one message per call and returns >0 while
     * more remain queued. */
    for (;;) {
        int r = sd_bus_process(ss.bus, NULL);
        if (r > 0) continue;
        if (r == 0) break;

        wlr_log(WLR_ERROR, "synui: screensaver: bus error: %s — disabling",
                strerror(-r));
        screensaver_finish(s);
        return 0;
    }

    sd_bus_flush(ss.bus);
    return 0;
}

void screensaver_init(syn_server_t *s)
{
    memset(&ss, 0, sizeof(ss));

    int r = sd_bus_open_user(&ss.bus);
    if (r < 0) {
        /* No session bus (e.g. the ISO's root session). Not fatal: the Wayland
         * idle-inhibit protocol still works, we just don't serve D-Bus. */
        wlr_log(WLR_INFO, "synui: screensaver: no session bus (%s) — "
                "D-Bus idle inhibit unavailable", strerror(-r));
        ss.bus = NULL;
        return;
    }

    /* Both paths: the spec says /org/freedesktop/ScreenSaver, but a good number
     * of apps (Chrome among them) call /ScreenSaver instead, and an app that
     * picks the wrong one silently gets no inhibit. Serve both. */
    static const char *paths[] = {
        "/org/freedesktop/ScreenSaver",
        "/ScreenSaver",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        r = sd_bus_add_object_vtable(ss.bus, NULL, paths[i],
                                     "org.freedesktop.ScreenSaver",
                                     ss_vtable, s);
        if (r < 0) {
            wlr_log(WLR_ERROR, "synui: screensaver: cannot export %s: %s",
                    paths[i], strerror(-r));
            goto fail;
        }
    }

    r = sd_bus_match_signal(ss.bus, NULL, "org.freedesktop.DBus",
                            "/org/freedesktop/DBus", "org.freedesktop.DBus",
                            "NameOwnerChanged", ss_name_owner_changed, s);
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: screensaver: cannot watch NameOwnerChanged: %s",
                strerror(-r));
        goto fail;
    }

    r = sd_bus_request_name(ss.bus, "org.freedesktop.ScreenSaver", 0);
    if (r < 0) {
        /* Someone else already owns it (a stray xfce4-power-manager, say).
         * Better to leave them to it than to fight over the name. */
        wlr_log(WLR_ERROR, "synui: screensaver: cannot take the name: %s",
                strerror(-r));
        goto fail;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    ss.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(ss.bus),
                                  WL_EVENT_READABLE, ss_readable, s);
    if (!ss.src) goto fail;

    wlr_log(WLR_INFO, "synui: screensaver: serving org.freedesktop.ScreenSaver");
    return;

fail:
    sd_bus_unref(ss.bus);
    ss.bus = NULL;
}

void screensaver_finish(syn_server_t *s)
{
    if (ss.src) {
        wl_event_source_remove(ss.src);
        ss.src = NULL;
    }
    if (ss.bus) {
        sd_bus_unref(ss.bus);
        ss.bus = NULL;
    }

    /* Anything we were holding is gone with the bus; do not leave the idle
     * stages disarmed forever because a dead inhibitor is still counted. */
    ss.n = 0;
    s->screensaver_inhibitors = 0;
}
