/*
 * notif.c — org.freedesktop.Notifications, so anything can tell you something.
 *
 * Nothing owned this name, so every notification on SYNAPSE failed silently:
 * Firefox, chibi, synguard's alerts, and synui-screenshot's own "saved" toast —
 * whose script still carries a comment explaining that SYNAPSE ships no
 * notification daemon and guards the call because of it. A screenshot bind that
 * gives no feedback at all is the state this replaces.
 *
 * Native rather than mako, for the reason the start menu and Bluetooth are: the
 * compositor already owns the screen. It can put a toast in the usable area
 * (below waybar's exclusive zone), above every window, on the output you are
 * actually looking at — with no layer-shell client, no second process to keep
 * alive, and no chance of the thing that reports failures being the thing that
 * died.
 *
 * The bus fd lives in the wl_event_loop (screensaver.c's idiom, same as bt.c),
 * and expiry is one timer armed to the *next* deadline rather than a timer per
 * toast or a poll.
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
#include <time.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

#define NOTIF_BUS_NAME "org.freedesktop.Notifications"
#define NOTIF_BUS_PATH "/org/freedesktop/Notifications"

/* Server default when a client passes expire_timeout = -1. */
#define NOTIF_DEFAULT_MS 5000

static struct {
    sd_bus                 *bus;
    struct wl_event_source *src;
    struct wl_event_source *timer;
} nf;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ── Closing ─────────────────────────────────────────────── */

static void notif_emit_closed(syn_server_t *s, uint32_t id, uint32_t reason)
{
    (void)s;
    if (!nf.bus) return;
    /* Clients watch this to know an expiry from a dismissal — some re-post on
     * one and not the other, and a daemon that never emits it leaves them
     * believing a toast is still up forever. */
    sd_bus_emit_signal(nf.bus, NOTIF_BUS_PATH, NOTIF_BUS_NAME,
                       "NotificationClosed", "uu", id, reason);
}

static void notif_arm_timer(syn_server_t *s);

static void notif_remove_at(syn_server_t *s, int i, uint32_t reason)
{
    syn_notifs_t *n = &s->notifs;
    if (i < 0 || i >= n->count) return;

    uint32_t id = n->items[i].id;
    memmove(&n->items[i], &n->items[i + 1],
            (size_t)(n->count - i - 1) * sizeof(n->items[0]));
    n->count--;
    notif_emit_closed(s, id, reason);
}

/* ── Expiry ──────────────────────────────────────────────── */

static int notif_expire_cb(void *data)
{
    syn_server_t *s = data;
    syn_notifs_t *n = &s->notifs;
    int64_t t = now_ms();

    for (int i = n->count - 1; i >= 0; i--) {
        if (n->items[i].expires_ms == 0) continue;      /* never expires */
        if (n->items[i].expires_ms <= t)
            notif_remove_at(s, i, NOTIF_CLOSED_EXPIRED);
    }

    synui_render_notifs(s);
    notif_arm_timer(s);
    return 0;
}

/* One timer, armed to the nearest deadline. A timer per toast would be fine
 * too, but this way there is exactly one thing to reason about — and nothing
 * polls, so an idle desktop with no toasts wakes up never. */
static void notif_arm_timer(syn_server_t *s)
{
    if (!nf.timer) return;
    syn_notifs_t *n = &s->notifs;

    int64_t soonest = 0;
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].expires_ms == 0) continue;
        if (!soonest || n->items[i].expires_ms < soonest)
            soonest = n->items[i].expires_ms;
    }

    if (!soonest) { wl_event_source_timer_update(nf.timer, 0); return; }

    int64_t delay = soonest - now_ms();
    if (delay < 1) delay = 1;      /* 0 disarms the timer — never pass it */
    if (delay > INT32_MAX) delay = INT32_MAX;
    wl_event_source_timer_update(nf.timer, (int)delay);
}

/* ── Text ────────────────────────────────────────────────── */

/* Notification text is arbitrary — it comes from any app on the box, and its
 * content from the network as often as not. cairo_show_text() poisons its whole
 * context on invalid UTF-8, and every later draw call silently becomes a no-op:
 * one bad body would blank every toast under it. So text is cut only on a
 * character boundary (news_utf8_trim is the codebase's one true cutter), and
 * anything that still will not validate is dropped rather than drawn.
 *
 * Newlines and control characters are flattened to spaces: a body is a card,
 * not a document, and a raw \n would draw as a box glyph mid-sentence. */
static void notif_text(char *dst, size_t cap, const char *src)
{
    dst[0] = '\0';
    if (!src) return;

    size_t len = strlen(src);
    if (len > cap - 1) len = cap - 1;
    len = news_utf8_trim(src, len);
    if (!len) return;

    memcpy(dst, src, len);
    dst[len] = '\0';

    for (char *c = dst; *c; c++)
        if ((unsigned char)*c < 0x20) *c = ' ';
}

/* ── Notify ──────────────────────────────────────────────── */

static int method_notify(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    syn_notifs_t *n = &s->notifs;

    const char *app, *icon, *summary, *body;
    uint32_t replaces;
    int32_t expire;

    int r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
    if (r < 0) return r;

    /* actions (as) — not implemented, so not advertised in GetCapabilities
     * either; a client that checks will not offer buttons it cannot get back.
     * Skipped rather than parsed, but it still has to be stepped over to reach
     * the hints. */
    r = sd_bus_message_skip(m, "as");
    if (r < 0) return r;

    int urgency = NOTIF_URGENCY_NORMAL;
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        const char *key;
        if (sd_bus_message_read(m, "s", &key) >= 0) {
            char type; const char *contents;
            if (sd_bus_message_peek_type(m, &type, &contents) >= 0 &&
                sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents) >= 0) {
                uint8_t u;
                if (strcmp(key, "urgency") == 0 && sd_bus_message_read(m, "y", &u) >= 0)
                    urgency = u;
                else
                    sd_bus_message_skip(m, contents);
                sd_bus_message_exit_container(m);
            }
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    r = sd_bus_message_read(m, "i", &expire);
    if (r < 0) return r;

    /* Find the slot: a replaces_id updates in place (a progress notification
     * re-posts the same id many times a second — appending would flood the
     * stack), otherwise append, dropping the oldest if full. */
    syn_notif_t *item = NULL;
    if (replaces) {
        for (int i = 0; i < n->count; i++)
            if (n->items[i].id == replaces) { item = &n->items[i]; break; }
    }
    if (!item) {
        if (n->count >= NOTIF_MAX)
            notif_remove_at(s, 0, NOTIF_CLOSED_EXPIRED);
        item = &n->items[n->count++];
        memset(item, 0, sizeof(*item));
        /* An id of 0 means "none" to callers, so never hand one out. */
        item->id = replaces ? replaces : ++n->next_id;
        if (item->id == 0) item->id = ++n->next_id;
    }

    notif_text(item->app,     sizeof(item->app),     app);
    notif_text(item->summary, sizeof(item->summary), summary);
    notif_text(item->body,    sizeof(item->body),    body);
    item->urgency = urgency;

    /* expire_timeout: -1 = server default, 0 = never. Critical is never
     * auto-expired regardless of what the client asked for — the spec is
     * explicit, and it is the whole point of the urgency. */
    if (urgency == NOTIF_URGENCY_CRITICAL)  item->expires_ms = 0;
    else if (expire == 0)                   item->expires_ms = 0;
    else if (expire < 0)                    item->expires_ms = now_ms() + NOTIF_DEFAULT_MS;
    else                                    item->expires_ms = now_ms() + expire;

    synui_render_notifs(s);
    notif_arm_timer(s);

    wlr_log(WLR_DEBUG, "synui: notif: #%u from %s: %s", item->id, item->app,
            item->summary);
    return sd_bus_reply_method_return(m, "u", item->id);
}

static int method_close(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    uint32_t id;
    int r = sd_bus_message_read(m, "u", &id);
    if (r < 0) return r;

    for (int i = 0; i < s->notifs.count; i++) {
        if (s->notifs.items[i].id != id) continue;
        notif_remove_at(s, i, NOTIF_CLOSED_BY_CALL);
        synui_render_notifs(s);
        notif_arm_timer(s);
        break;
    }
    /* Closing an unknown id is not an error per the spec — it may have expired
     * a moment ago, which is a race every client would otherwise have to lose. */
    return sd_bus_reply_method_return(m, "");
}

/* Advertise only what is actually implemented. Claiming "actions" would make
 * clients offer buttons that can never be clicked, and claiming "body-markup"
 * would make them send <b>bold</b> that draws literally. "body" and "persistence"
 * are true: bodies are rendered, and critical toasts stay until dismissed. */
static int method_get_caps(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)data; (void)e;
    return sd_bus_reply_method_return(m, "as", 2, "body", "persistence");
}

static int method_get_info(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)data; (void)e;
    return sd_bus_reply_method_return(m, "ssss", "synui", "SynapseOS", "1.0", "1.2");
}

static const sd_bus_vtable notif_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", method_notify,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", method_close,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCapabilities", "", "as", method_get_caps,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", method_get_info,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
    SD_BUS_SIGNAL("ActionInvoked", "us", 0),
    SD_BUS_VTABLE_END
};

/* ── Click to dismiss ────────────────────────────────────── */

int notif_click(syn_server_t *s, double lx, double ly)
{
    syn_notifs_t *n = &s->notifs;
    if (!n->count) return 0;

    struct wlr_box stack;
    int hit = synui_notif_hit(s, lx, ly, &stack);
    if (hit < 0) return 0;

    notif_remove_at(s, hit, NOTIF_CLOSED_DISMISSED);
    synui_render_notifs(s);
    notif_arm_timer(s);
    return 1;
}

/* ── Event loop ──────────────────────────────────────────── */

static int notif_readable(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    syn_server_t *s = data;

    for (;;) {
        int r = sd_bus_process(nf.bus, NULL);
        if (r > 0) continue;
        if (r == 0) break;
        wlr_log(WLR_ERROR, "synui: notif: bus error: %s — disabling", strerror(-r));
        notif_finish(s);
        return 0;
    }
    sd_bus_flush(nf.bus);
    return 0;
}

void notif_init(syn_server_t *s)
{
    memset(&nf, 0, sizeof(nf));
    memset(&s->notifs, 0, sizeof(s->notifs));

    int r = sd_bus_open_user(&nf.bus);
    if (r < 0) {
        wlr_log(WLR_INFO, "synui: notif: no session bus (%s) — no toasts",
                strerror(-r));
        nf.bus = NULL;
        return;
    }

    r = sd_bus_add_object_vtable(nf.bus, NULL, NOTIF_BUS_PATH, NOTIF_BUS_NAME,
                                 notif_vtable, s);
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: notif: cannot export object: %s", strerror(-r));
        goto fail;
    }

    r = sd_bus_request_name(nf.bus, NOTIF_BUS_NAME, 0);
    if (r < 0) {
        /* Someone else has it — a mako or dunst left running. Two daemons on one
         * name is worse than one: leave them to it rather than fight, the same
         * call screensaver.c makes. */
        wlr_log(WLR_ERROR, "synui: notif: cannot take %s (%s) — "
                "another notification daemon is running", NOTIF_BUS_NAME,
                strerror(-r));
        goto fail;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    nf.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(nf.bus), WL_EVENT_READABLE,
                                  notif_readable, s);
    if (!nf.src) goto fail;
    nf.timer = wl_event_loop_add_timer(loop, notif_expire_cb, s);
    if (!nf.timer) goto fail;

    wlr_log(WLR_INFO, "synui: notif: serving %s", NOTIF_BUS_NAME);
    return;

fail:
    if (nf.src) { wl_event_source_remove(nf.src); nf.src = NULL; }
    sd_bus_unref(nf.bus);
    nf.bus = NULL;
}

void notif_finish(syn_server_t *s)
{
    /* Tell whoever is still listening that their toasts are gone, rather than
     * leaving clients believing they are up. */
    for (int i = s->notifs.count - 1; i >= 0; i--)
        notif_emit_closed(s, s->notifs.items[i].id, NOTIF_CLOSED_UNDEFINED);
    s->notifs.count = 0;

    if (nf.timer) { wl_event_source_remove(nf.timer); nf.timer = NULL; }
    if (nf.src)   { wl_event_source_remove(nf.src);   nf.src = NULL; }
    if (nf.bus)   { sd_bus_flush(nf.bus); sd_bus_unref(nf.bus); nf.bus = NULL; }
}
