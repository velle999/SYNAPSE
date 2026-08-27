/*
 * mpris.c — what is playing, for the screens that have no bar.
 *
 * The desktop already answers "what is playing" twice: the bar's Media module
 * (quickshell/modules/Media.qml, via playerctl) and cliamp's own window. Both
 * are CLIENTS, and the two screens this file exists for have no clients on them
 * at all — the lock screen and the login screen are drawn by the compositor,
 * over a session whose windows are hidden or which does not exist yet. Pausing
 * the music you locked the machine on meant unlocking it first.
 *
 * So the compositor speaks MPRIS itself. This is a READER and three verbs
 * (PlayPause / Next / Previous) — no seeking, no playlists, no art: everything
 * the lock panel can show and every button it can draw, and nothing else.
 *
 * ⚠ EVERY CALL IS ASYNC. A media player is an ordinary desktop app — an
 * Electron one, often — and sd_bus_call() on a wedged app blocks for the
 * request timeout. This runs on the compositor's event loop, so a synchronous
 * property read would freeze every window on the desktop for as long as some
 * browser tab took to answer. Nothing here waits for a reply; replies arrive on
 * the loop like any other message.
 *
 * ⚠ SIGNALS COME FROM THE UNIQUE NAME, NOT THE WELL-KNOWN ONE. A player is
 * addressed as `org.mpris.MediaPlayer2.cliamp`, but its PropertiesChanged
 * arrives from `:1.42`, so a table keyed only on the pretty name matches
 * nothing and the panel never updates. Each player's owner is tracked
 * (GetNameOwner at startup, NameOwnerChanged after) and the signal is matched
 * on that.
 *
 * No session bus is not an error: the ISO's root session has none, and the
 * greeter runs as an account that has no players on it either. Both simply get
 * a lock screen with no media row, which is what they should have.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PATH   "/org/mpris/MediaPlayer2"
#define MPRIS_IFACE  "org.mpris.MediaPlayer2.Player"

/* Bound on how long a reply may stay outstanding. The call is async, so this
 * is not a stall — it only stops a player that never answers from leaving a
 * pending slot (and a stale "Playing") behind forever. */
#define MPRIS_CALL_TIMEOUT_US (3 * 1000 * 1000)

#define MPRIS_MAX_PLAYERS 8

typedef struct {
    char     bus[96];        /* org.mpris.MediaPlayer2.<player> */
    char     owner[32];      /* :1.42 — who its signals actually come from */
    char     title[192];
    char     artist[160];
    int      playing;
    int      can_next, can_prev, can_pause;
    uint32_t seen_ms;        /* last update; picks between two live players */
    bool     used;
} syn_mpris_player_t;

static struct {
    sd_bus                 *bus;
    struct wl_event_source *src;
    syn_mpris_player_t      p[MPRIS_MAX_PLAYERS];
    syn_server_t           *server;
} mp;

static uint32_t mpris_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── The table ───────────────────────────────────────────── */

static syn_mpris_player_t *mpris_by_bus(const char *name)
{
    for (int i = 0; i < MPRIS_MAX_PLAYERS; i++)
        if (mp.p[i].used && strcmp(mp.p[i].bus, name) == 0) return &mp.p[i];
    return NULL;
}

static syn_mpris_player_t *mpris_by_owner(const char *owner)
{
    if (!owner || !*owner) return NULL;
    for (int i = 0; i < MPRIS_MAX_PLAYERS; i++)
        if (mp.p[i].used && strcmp(mp.p[i].owner, owner) == 0) return &mp.p[i];
    return NULL;
}

static syn_mpris_player_t *mpris_add(const char *name)
{
    syn_mpris_player_t *e = mpris_by_bus(name);
    if (e) return e;
    for (int i = 0; i < MPRIS_MAX_PLAYERS; i++) {
        if (mp.p[i].used) continue;
        memset(&mp.p[i], 0, sizeof(mp.p[i]));
        snprintf(mp.p[i].bus, sizeof(mp.p[i].bus), "%s", name);
        mp.p[i].used = true;
        mp.p[i].seen_ms = mpris_now_ms();
        return &mp.p[i];
    }
    return NULL;                 /* nine players at once: the ninth is ignored */
}

static void mpris_drop(const char *name)
{
    syn_mpris_player_t *e = mpris_by_bus(name);
    if (e) memset(e, 0, sizeof(*e));
}

/*
 * The player the lock screen is about.
 *
 * Something PLAYING always wins over something paused — with a browser tab and
 * a music player both on the bus, the one making noise is the one the buttons
 * should reach. Ties go to whichever spoke most recently, which is the same
 * "most recently used" the bar picks by.
 */
static syn_mpris_player_t *mpris_current(void)
{
    syn_mpris_player_t *best = NULL;
    for (int i = 0; i < MPRIS_MAX_PLAYERS; i++) {
        syn_mpris_player_t *e = &mp.p[i];
        if (!e->used) continue;
        if (!e->title[0] && !e->artist[0]) continue;   /* nothing to show */
        if (!best) { best = e; continue; }
        if (e->playing != best->playing) {
            if (e->playing) best = e;
            continue;
        }
        if ((int32_t)(e->seen_ms - best->seen_ms) > 0) best = e;
    }
    return best;
}

/* ── Property parsing ────────────────────────────────────── */

/*
 * cliamp reports a FILE PATH as its title and no artist at all — see
 * reference-cliamp-mpris-title-is-watch. Drawn verbatim that would fill the
 * media row with `/home/velle/Music/…/07 - track.flac`, so a title that is
 * plainly a path is cut to its basename with the extension dropped. A title
 * that is not a path is never touched: "AC/DC — Back in Black" has a slash in
 * it and is not a file name.
 */
static void mpris_clean_title(char *t, size_t n)
{
    if (t[0] != '/' && strncmp(t, "file://", 7) != 0) return;

    const char *base = strrchr(t, '/');
    base = base ? base + 1 : t;

    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%s", base);
    char *dot = strrchr(tmp, '.');
    /* Only a short, extension-shaped tail — a dot in the middle of a name
     * ("Mr. Blue Sky") is not a file extension. */
    if (dot && dot != tmp && strlen(dot) <= 5) *dot = '\0';
    snprintf(t, n, "%s", tmp);
}

/* Read one variant whose contents are `sig`, or skip it. Returns 1 if the
 * caller's `out` was written. */
static int read_variant_string(sd_bus_message *m, char *out, size_t n)
{
    const char *v = NULL;
    int r = sd_bus_message_read(m, "v", "s", &v);
    if (r < 0 || !v) return 0;
    snprintf(out, n, "%s", v);
    return 1;
}

/* xesam:artist is an ARRAY of strings. Joined with ", " — most tracks have
 * one, a collaboration has three, and a row that showed only the first would
 * quietly rewrite the credits. */
static int read_variant_strv(sd_bus_message *m, char *out, size_t n)
{
    int r = sd_bus_message_enter_container(m, 'v', "as");
    if (r <= 0) return 0;
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) { sd_bus_message_exit_container(m); return 0; }

    out[0] = '\0';
    const char *v;
    size_t used = 0;
    while (sd_bus_message_read(m, "s", &v) > 0) {
        if (!v || !*v) continue;
        int w = snprintf(out + used, n - used, "%s%s", used ? ", " : "", v);
        if (w < 0 || (size_t)w >= n - used) { used = n - 1; break; }
        used += (size_t)w;
    }
    sd_bus_message_exit_container(m);
    sd_bus_message_exit_container(m);
    return 1;
}

/* The Metadata dict: a{sv} keyed by xesam:*. Anything we do not name is
 * skipped, which is most of it (art, length, track ids, per-player extras). */
static void mpris_read_metadata(sd_bus_message *m, syn_mpris_player_t *e)
{
    if (sd_bus_message_enter_container(m, 'v', "a{sv}") <= 0) return;
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
        sd_bus_message_exit_container(m);
        return;
    }

    e->title[0] = '\0';
    e->artist[0] = '\0';

    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char *key = NULL;
        if (sd_bus_message_read(m, "s", &key) < 0 || !key) {
            sd_bus_message_exit_container(m);
            break;
        }
        if (strcmp(key, "xesam:title") == 0) {
            if (read_variant_string(m, e->title, sizeof(e->title)))
                mpris_clean_title(e->title, sizeof(e->title));
            else
                sd_bus_message_skip(m, "v");
        } else if (strcmp(key, "xesam:artist") == 0 ||
                   strcmp(key, "xesam:albumArtist") == 0) {
            /* albumArtist only fills in when xesam:artist did not — a
             * compilation names both and the track artist is the right one. */
            if (e->artist[0]) sd_bus_message_skip(m, "v");
            else if (!read_variant_strv(m, e->artist, sizeof(e->artist)))
                sd_bus_message_skip(m, "v");
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }

    sd_bus_message_exit_container(m);
    sd_bus_message_exit_container(m);
}

/* One a{sv} of Player properties — the body of both GetAll's reply and
 * PropertiesChanged's second argument, which is why it is one function. */
static void mpris_read_props(sd_bus_message *m, syn_mpris_player_t *e)
{
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) return;

    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char *key = NULL;
        if (sd_bus_message_read(m, "s", &key) < 0 || !key) {
            sd_bus_message_exit_container(m);
            break;
        }
        if (strcmp(key, "Metadata") == 0) {
            mpris_read_metadata(m, e);
        } else if (strcmp(key, "PlaybackStatus") == 0) {
            char st[32];
            if (read_variant_string(m, st, sizeof(st)))
                e->playing = strcmp(st, "Playing") == 0;
            else
                sd_bus_message_skip(m, "v");
        } else if (strcmp(key, "CanGoNext") == 0 ||
                   strcmp(key, "CanGoPrevious") == 0 ||
                   strcmp(key, "CanPause") == 0) {
            int b = 0;
            if (sd_bus_message_read(m, "v", "b", &b) < 0) {
                sd_bus_message_skip(m, "v");
            } else if (key[3] == 'G' && key[6] == 'N') {
                e->can_next = b;
            } else if (key[3] == 'G') {
                e->can_prev = b;
            } else {
                e->can_pause = b;
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

/* Redraw whatever is showing this. Only the lock screen draws a media row, and
 * only while it is up — off the lock this costs one branch per property
 * change, which is a few per track. */
static void mpris_changed(void)
{
    if (mp.server && mp.server->nlock.active) lock_render(mp.server);
}

/* ── Bus traffic ─────────────────────────────────────────── */

static int mpris_on_getall(sd_bus_message *m, void *data, sd_bus_error *err)
{
    (void)err;
    char *name = data;

    syn_mpris_player_t *e = mpris_by_bus(name);
    free(name);
    if (!e) return 0;                          /* player went away mid-flight */

    if (sd_bus_message_is_method_error(m, NULL)) return 0;

    mpris_read_props(m, e);
    e->seen_ms = mpris_now_ms();
    mpris_changed();
    return 0;
}

static void mpris_query(syn_mpris_player_t *e)
{
    if (!mp.bus || !e) return;

    sd_bus_message *m = NULL;
    if (sd_bus_message_new_method_call(mp.bus, &m, e->bus, MPRIS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "GetAll") < 0)
        return;
    if (sd_bus_message_append(m, "s", MPRIS_IFACE) < 0) {
        sd_bus_message_unref(m);
        return;
    }
    /* strdup'd rather than a pointer into the table: the table entry can be
     * dropped (the player quit) before the reply lands, and the callback has to
     * be able to ask whether it still exists. */
    char *tag = strdup(e->bus);
    if (!tag) { sd_bus_message_unref(m); return; }
    if (sd_bus_call_async(mp.bus, NULL, m, mpris_on_getall, tag,
                          MPRIS_CALL_TIMEOUT_US) < 0)
        free(tag);
    sd_bus_message_unref(m);
}

static int mpris_on_owner(sd_bus_message *m, void *data, sd_bus_error *err)
{
    (void)err;
    char *name = data;

    syn_mpris_player_t *e = mpris_by_bus(name);
    free(name);
    if (!e || sd_bus_message_is_method_error(m, NULL)) return 0;

    const char *owner = NULL;
    if (sd_bus_message_read(m, "s", &owner) >= 0 && owner)
        snprintf(e->owner, sizeof(e->owner), "%s", owner);
    return 0;
}

static void mpris_ask_owner(syn_mpris_player_t *e)
{
    if (!mp.bus || !e) return;
    sd_bus_message *m = NULL;
    if (sd_bus_message_new_method_call(mp.bus, &m, "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "GetNameOwner") < 0)
        return;
    if (sd_bus_message_append(m, "s", e->bus) < 0) {
        sd_bus_message_unref(m);
        return;
    }
    char *tag = strdup(e->bus);
    if (!tag) { sd_bus_message_unref(m); return; }
    if (sd_bus_call_async(mp.bus, NULL, m, mpris_on_owner, tag,
                          MPRIS_CALL_TIMEOUT_US) < 0)
        free(tag);
    sd_bus_message_unref(m);
}

static int mpris_on_listnames(sd_bus_message *m, void *data, sd_bus_error *err)
{
    (void)data; (void)err;
    if (sd_bus_message_is_method_error(m, NULL)) return 0;
    if (sd_bus_message_enter_container(m, 'a', "s") <= 0) return 0;

    const char *n;
    while (sd_bus_message_read(m, "s", &n) > 0) {
        if (strncmp(n, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) != 0) continue;
        syn_mpris_player_t *e = mpris_add(n);
        if (!e) continue;
        mpris_ask_owner(e);
        mpris_query(e);
    }
    sd_bus_message_exit_container(m);
    return 0;
}

static int mpris_name_owner_changed(sd_bus_message *m, void *data,
                                    sd_bus_error *err)
{
    (void)data; (void)err;
    const char *name = NULL, *old = NULL, *neu = NULL;
    if (sd_bus_message_read(m, "sss", &name, &old, &neu) < 0 || !name) return 0;
    if (strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) != 0) return 0;

    if (neu && *neu) {
        syn_mpris_player_t *e = mpris_add(name);
        if (e) {
            snprintf(e->owner, sizeof(e->owner), "%s", neu);
            mpris_query(e);
        }
    } else {
        mpris_drop(name);
        mpris_changed();
    }
    return 0;
}

static int mpris_props_changed(sd_bus_message *m, void *data, sd_bus_error *err)
{
    (void)data; (void)err;

    syn_mpris_player_t *e = mpris_by_owner(sd_bus_message_get_sender(m));
    if (!e) return 0;              /* someone else's PropertiesChanged */

    const char *iface = NULL;
    if (sd_bus_message_read(m, "s", &iface) < 0 || !iface) return 0;
    if (strcmp(iface, MPRIS_IFACE) != 0) return 0;

    mpris_read_props(m, e);
    e->seen_ms = mpris_now_ms();
    mpris_changed();
    return 0;
}

/* ── Public: what to draw ────────────────────────────────── */

/*
 * The one-line answer the lock panel draws, or false when there is nothing to
 * say. `playing` distinguishes the ⏸ glyph from the ▶ one; the can_* flags dim
 * the buttons a player says it will not honour.
 */
bool mpris_now_playing(syn_mpris_now_t *out)
{
    syn_mpris_player_t *e = mpris_current();
    if (!e) return false;

    memset(out, 0, sizeof(*out));
    snprintf(out->title,  sizeof(out->title),  "%s", e->title);
    snprintf(out->artist, sizeof(out->artist), "%s", e->artist);
    out->playing  = e->playing;
    out->can_next = e->can_next;
    out->can_prev = e->can_prev;
    /* A player that says CanPause=false can still be STARTED, and PlayPause is
     * one method for both — so the middle button is live whenever anything is
     * there, and only a paused-and-unpausable player would be wrong, which is
     * not a state MPRIS has. */
    out->can_play = 1;
    return true;
}

/* One verb at the current player. Fire and forget: the reply carries nothing,
 * and the state that matters comes back as PropertiesChanged. */
static void mpris_verb(const char *member)
{
    syn_mpris_player_t *e = mpris_current();
    if (!mp.bus || !e) return;
    int r = sd_bus_call_method_async(mp.bus, NULL, e->bus, MPRIS_PATH,
                                     MPRIS_IFACE, member, NULL, NULL, NULL);
    if (r < 0)
        wlr_log(WLR_INFO, "synui: mpris: %s on %s: %s", member, e->bus,
                strerror(-r));
}

void mpris_playpause(void) { mpris_verb("PlayPause"); }
void mpris_next(void)      { mpris_verb("Next"); }
void mpris_previous(void)  { mpris_verb("Previous"); }

/* ── Lifecycle ───────────────────────────────────────────── */

static int mpris_readable(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    syn_server_t *s = data;

    for (;;) {
        int r = sd_bus_process(mp.bus, NULL);
        if (r > 0) continue;
        if (r == 0) break;
        wlr_log(WLR_ERROR, "synui: mpris: bus error: %s — disabling",
                strerror(-r));
        mpris_finish(s);
        return 0;
    }
    sd_bus_flush(mp.bus);
    return 0;
}

void mpris_init(syn_server_t *s)
{
    memset(&mp, 0, sizeof(mp));
    mp.server = s;

    int r = sd_bus_open_user(&mp.bus);
    if (r < 0) {
        wlr_log(WLR_INFO, "synui: mpris: no session bus (%s) — the lock screen "
                "shows no media row", strerror(-r));
        mp.bus = NULL;
        return;
    }

    /* Players appearing and disappearing. arg0namespace keeps the broker from
     * waking us for every name on the bus. */
    r = sd_bus_add_match(mp.bus, NULL,
                         "type='signal',sender='org.freedesktop.DBus',"
                         "interface='org.freedesktop.DBus',"
                         "member='NameOwnerChanged',"
                         "arg0namespace='org.mpris.MediaPlayer2'",
                         mpris_name_owner_changed, s);
    if (r < 0) goto fail;

    /* Track and status changes. Every MPRIS player exports the same path, so
     * the path alone is a tight enough filter; the sender is checked against
     * the table in the handler. */
    r = sd_bus_add_match(mp.bus, NULL,
                         "type='signal',path='" MPRIS_PATH "',"
                         "interface='org.freedesktop.DBus.Properties',"
                         "member='PropertiesChanged'",
                         mpris_props_changed, s);
    if (r < 0) goto fail;

    /* Who is already playing. Async like everything else — synui does not wait
     * on the bus at startup. */
    r = sd_bus_call_method_async(mp.bus, NULL, "org.freedesktop.DBus",
                                 "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                 "ListNames", mpris_on_listnames, s, NULL);
    if (r < 0) goto fail;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    mp.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(mp.bus),
                                  WL_EVENT_READABLE, mpris_readable, s);
    if (!mp.src) goto fail;

    wlr_log(WLR_INFO, "synui: mpris: watching for players");
    return;

fail:
    wlr_log(WLR_ERROR, "synui: mpris: cannot watch the bus (%s) — disabled",
            strerror(-r));
    sd_bus_unref(mp.bus);
    mp.bus = NULL;
}

void mpris_finish(syn_server_t *s)
{
    (void)s;
    if (mp.src) { wl_event_source_remove(mp.src); mp.src = NULL; }
    if (mp.bus) { sd_bus_unref(mp.bus); mp.bus = NULL; }
    memset(mp.p, 0, sizeof(mp.p));
}
