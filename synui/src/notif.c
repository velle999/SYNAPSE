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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "i18n.h"

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
 * content from the network as often as not. syn_show_text() poisons its whole
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

/*
 * Put a toast on the stack. The body of org.freedesktop.Notifications.Notify,
 * split out so synui can post its own — a compositor that runs the notification
 * daemon should not have to go out over the bus and back to tell the user what
 * a keybinding just did.
 *
 * `replaces` is the same distinction the spec draws and everything below relies
 * on: 0 appends a new toast (and chimes), a live id updates that one in place
 * (and does not). A binding the user can hold down wants the second, or every
 * press stacks another card.
 *
 * `expire` follows the protocol: -1 server default, 0 never.
 * Returns the id, which is what a caller passes back as `replaces` next time.
 */
uint32_t notif_post(syn_server_t *s, const char *app, const char *summary,
                    const char *body, int urgency, int32_t expire,
                    uint32_t replaces)
{
    return notif_post_ex(s, app, summary, body, urgency, expire, replaces, false);
}

uint32_t notif_post_ex(syn_server_t *s, const char *app, const char *summary,
                       const char *body, int urgency, int32_t expire,
                       uint32_t replaces, bool dnd_bypass)
{
    syn_notifs_t *n = &s->notifs;

    /* ── Do Not Disturb ──────────────────────────────────────────────────────
     *
     * Swallowed here, at the top, so BOTH halves are covered by one rule: no
     * card is allocated (nothing to draw) and sound_play() below is never
     * reached (nothing to hear). Suppressing only the draw would leave the
     * chime, which is the half people actually notice.
     *
     * It still returns an id, and a real one. A client that gets 0 or a D-Bus
     * error back concludes the desktop has no notification daemon, and the
     * well-behaved ones then fall back to drawing their own window — a mode
     * meant to stop interruptions would start producing windows that cannot be
     * dismissed by anything here. The id is drawn from the same counter, so
     * CloseNotification() on it is a harmless no-op rather than a mismatch.
     *
     * CRITICAL goes through. The spec singles it out as the urgency that must
     * not auto-expire, synguard posts its intrusion alerts at it, and a quiet
     * mode that can hide a security alert is a bug wearing a feature's name. */
    if (!dnd_bypass && s->config.notif_dnd && urgency != NOTIF_URGENCY_CRITICAL) {
        if (n->missed < INT_MAX) n->missed++;
        wlr_log(WLR_DEBUG, "synui: notif: dnd swallowed '%s' from %s (%d missed)",
                summary ? summary : "", app ? app : "?", n->missed);
        if (replaces) return replaces;
        uint32_t id = ++n->next_id;
        if (id == 0) id = ++n->next_id;
        return id;
    }

    /* Find the slot: a replaces_id updates in place (a progress notification
     * re-posts the same id many times a second — appending would flood the
     * stack), otherwise append, dropping the oldest if full. */
    syn_notif_t *item = NULL;
    if (replaces) {
        for (int i = 0; i < n->count; i++)
            if (n->items[i].id == replaces) { item = &n->items[i]; break; }
    }
    bool fresh = (item == NULL);
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

    /* Only for a NEW notification: a progress bar re-posting the same id many
     * times a second is one notification, and chiming per update would be a
     * machine-gun. Keyed on whether a slot was actually allocated rather than on
     * `replaces` alone, so a caller re-posting an id that has already expired
     * out from under it is a new toast and sounds like one. */
    if (fresh)
        sound_play(s, urgency == NOTIF_URGENCY_CRITICAL ? SOUND_EVT_ERROR
                                                        : SOUND_EVT_NOTIFY);

    wlr_log(WLR_DEBUG, "synui: notif: #%u from %s: %s", item->id, item->app,
            item->summary);
    return item->id;
}

/* ── Do Not Disturb ──────────────────────────────────────── */

static bool dnd_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "dnd.state");
}

void notif_dnd_state_save(syn_server_t *s)
{
    char path[256];
    if (!dnd_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: notif: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "dnd=%d\n", s->config.notif_dnd ? 1 : 0);
    fclose(f);
}

/*
 * Applied over the config, and read from synui_config_load()'s tail rather than
 * once at startup. That placement is the whole difference between a config
 * reload keeping the ringer off and a reload switching it back on: reload does
 * `s->config = fresh`, so notif_dnd comes back from the sources
 * synui_config_load() reads and from nowhere else. filters.state and theme.state
 * were each shipped with that bug and fixed the same way; this one would have
 * been worse, because the failure is silent until something makes a noise in a
 * meeting.
 */
void notif_dnd_state_load_config(syn_config_t *cfg)
{
    char path[256];
    if (!dnd_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* never saved — keep whatever synuirc said */

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, "dnd") == 0) cfg->notif_dnd = atoi(eq + 1) ? 1 : 0;
    }
    fclose(f);
}

void notif_dnd_set(syn_server_t *s, bool on)
{
    if (!!s->config.notif_dnd == on) return;
    s->config.notif_dnd = on ? 1 : 0;
    notif_dnd_state_save(s);

    if (on) {
        /* "disable popups too": clear what is already on screen. A card left
         * hanging when the mode is switched on is exactly the interruption
         * being switched off, and it would sit there for its full expiry.
         *
         * Backwards, because notif_remove_at() memmoves the tail down.
         * Critical toasts stay for the same reason they get through below. */
        for (int i = s->notifs.count - 1; i >= 0; i--)
            if (s->notifs.items[i].urgency != NOTIF_URGENCY_CRITICAL)
                notif_remove_at(s, i, NOTIF_CLOSED_DISMISSED);

        s->notifs.missed = 0;
        synui_render_notifs(s);
        notif_arm_timer(s);
    }

    /* The confirmation BYPASSES the mode it is announcing — posted after the
     * flag is set, and deliberately allowed through by notif_post()'s dnd_bypass
     * argument. Feedback for the keybinding that just ran is not an
     * interruption; without it, pressing the key while DND is already on looks
     * exactly like pressing a key that does nothing. */
    char body[96] = "";
    if (!on && s->notifs.missed > 0)
        snprintf(body, sizeof(body),
                 P_("%d notification arrived while it was on",
                    "%d notifications arrived while it was on", s->notifs.missed),
                 s->notifs.missed);

    if (!on) s->notifs.missed = 0;

    s->notifs.dnd_notif_id =
        notif_post_ex(s, "synui", on ? _("Do Not Disturb on") : _("Do Not Disturb off"),
                      body, NOTIF_URGENCY_NORMAL, on ? 2000 : -1,
                      s->notifs.dnd_notif_id, true);
}

void notif_dnd_toggle(syn_server_t *s)
{
    notif_dnd_set(s, !s->config.notif_dnd);
}

static int method_notify(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;

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

    uint32_t id = notif_post(s, app, summary, body, urgency, expire, replaces);
    return sd_bus_reply_method_return(m, "u", id);
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

    /* The expiry timer is armed before the bus, and survives every failure
     * below, because the bus is no longer the only thing that posts toasts:
     * notif_post() is called from inside synui (Super+Tab says which layout it
     * cycled to). Left where it was — after sd_bus_request_name — a box with a
     * mako or dunst already running got a NULL timer, notif_arm_timer()'s early
     * return, and synui's own toasts pinned to the screen forever. */
    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    nf.timer = wl_event_loop_add_timer(loop, notif_expire_cb, s);
    if (!nf.timer)
        wlr_log(WLR_ERROR, "synui: notif: no expiry timer — toasts will not fade");

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

    nf.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(nf.bus), WL_EVENT_READABLE,
                                  notif_readable, s);
    if (!nf.src) goto fail;

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
