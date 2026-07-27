/*
 * game.c — Game mode: give the machine back to the game.
 *
 * Detection. A fullscreen XWayland client is, on this desktop, almost always a
 * game: Steam titles, Proton/Wine, and native X11 games all present that way,
 * while modern desktop apps are Wayland-native and so never match. That is the
 * whole signal — plus an exclusion list for the fullscreen *X11* things that
 * are not games (a fullscreen Firefox video being the one that matters, since
 * suspending the AI to watch a video would be a nasty surprise).
 *
 * What it does while a game is up:
 *
 *   - Stops synapd. It pins ~4GB of VRAM and llama.cpp worker threads, and it
 *     has no unload/sleep IPC — synapd.h offers only RELOAD and SHUTDOWN — so
 *     stopping the service is the only way to hand the GPU and those cores to
 *     the game. It is started again on exit, and reloads its model then.
 *
 *   - Stops the synapd poller. With synapd down it would do nothing but spin on
 *     connect failures and log them.
 *
 *   - Holds off the idle stages. A gamepad is not a seat input device, so to the
 *     compositor a controller-only session looks perfectly idle: without this
 *     the screen dims, then blanks, mid-game. This is a separate flag from
 *     s->idle_inhibitors on purpose — that counter belongs to the wlr
 *     idle-inhibit protocol and is mirrored to the idle notifier, so game mode
 *     must not forge entries in it.
 *
 * Everything is restored on exit, including when synui itself is shutting down
 * (game_finish) — leaving the box with a stopped synapd because the compositor
 * died mid-game would be a lousy way to lose the AI.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "synui.h"

/* Publish the current state for the waybar indicator (custom/gamemode, which
 * runs synui-game-status). synui has no IPC to ask, so the state is pushed to a
 * file in XDG_RUNTIME_DIR — same reasoning as synui-display: 0700 and owned by
 * the session user, so nobody else can forge it.
 *
 * Written on every transition *and* once at startup, because a synui that died
 * mid-game would otherwise leave a file reading "on" and the bar would insist a
 * game was running until the next one ended.
 *
 * The app id lands in a file a shell-adjacent helper reads, and it comes from
 * the client's WM_CLASS — so it is quoted, not trusted: synui-game-status parses
 * it as data and JSON-escapes it. */
static void game_publish(syn_server_t *s)
{
    const char *rtdir = getenv("XDG_RUNTIME_DIR");
    if (!rtdir || !*rtdir) return;   /* headless test rig: nothing reads this */

    char path[256], tmp[256];
    snprintf(path, sizeof(path), "%s/synui-game", rtdir);
    snprintf(tmp,  sizeof(tmp),  "%s/synui-game.tmp", rtdir);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: game: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }
    fprintf(f, "state=%s\n", s->game.active ? "on" : "off");
    fprintf(f, "mode=%s\n", s->game.forced > 0 ? "forced-on" :
                            s->game.forced < 0 ? "forced-off" : "auto");
    fprintf(f, "app=%s\n", s->game.app);
    /* What we did about synapd, so the indicator stops asserting it. The bar
     * used to print "synapd suspended (GPU freed)" for any state=on, which is
     * how a stop that got undone by socket activation still read as success.
     * This is still only what synui *asked for* — synui-game-status checks
     * whether it held. */
    fprintf(f, "ai=%s\n", s->game.ai_suspended ? "suspended" :
                          s->config.game_suspend_ai ? "running" : "untouched");
    fclose(f);

    /* Rename, so the poller never reads a half-written file. */
    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: game: cannot publish '%s': %s",
                path, strerror(errno));
        unlink(tmp);
    }
}

/* Startup: stamp a known state, so a leftover file from a crashed synui cannot
 * leave the bar showing a game that is not running. */
void game_init(syn_server_t *s)
{
    game_publish(s);
}

/* Is this app_id one of the fullscreen-X11 things that is NOT a game?
 * Case-insensitive substring, so "firefox" also covers "Firefox"/"firefox-esr". */
static int game_excluded(const syn_config_t *cfg, const char *app_id)
{
    if (!app_id) return 0;
    for (int i = 0; i < cfg->game_exclude_count; i++)
        if (strcasestr(app_id, cfg->game_exclude[i])) return 1;
    return 0;
}

/* The first mapped, fullscreen, non-excluded XWayland view — or NULL.
 * Views live per-workspace, so a game on an inactive workspace still counts:
 * it is still rendering and still holding the GPU. */
static syn_view_t *game_find_view(syn_server_t *s)
{
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            if (!v->mapped || !v->fullscreen || !v->is_xwayland) continue;
            const char *aid = view_app_id(v);
            /* A NULL/empty WM_CLASS cannot match an exclusion, so it stays a
             * game: the fullscreen-X11 evidence stands on its own and the class
             * is only ever used to rule things out. Log it — an unexpected
             * trigger is exactly what a user would want to see. */
            if (!aid || !*aid)
                wlr_log(WLR_INFO, "synui: game: fullscreen X11 client with no "
                                  "WM_CLASS — treating as a game");
            else if (game_excluded(&s->config, aid))
                continue;
            return v;
        }
    }
    return NULL;
}

static void game_enter(syn_server_t *s, syn_view_t *v)
{
    const char *aid = (v && view_app_id(v)) ? view_app_id(v) : "unknown";
    snprintf(s->game.app, sizeof(s->game.app), "%s", aid);
    s->game.active = 1;
    wlr_log(WLR_INFO, "synui: game: ON (%s)", s->game.app);

    if (s->config.game_suspend_ai && !s->game.ai_suspended) {
        /* Stop the poller BEFORE the daemon, or it spends the shutdown window
         * reconnecting to a socket that is going away. */
        synmon_stop(s);
        synui_spawn(s->config.game_ai_stop_cmd);
        s->game.ai_suspended = 1;
        wlr_log(WLR_INFO, "synui: game: suspending synapd (freeing GPU/CPU) — `%s`",
                s->config.game_ai_stop_cmd);
    }

    /* Re-run the idle arming with game mode now set: power_arm() sees it and
     * leaves every stage disarmed. Also undoes a dim that already landed. */
    if (s->config.game_inhibit_idle) power_notify_activity(s);

    game_publish(s);
}

static void game_leave(syn_server_t *s)
{
    s->game.active = 0;
    s->game.app[0] = '\0';
    wlr_log(WLR_INFO, "synui: game: OFF");

    if (s->game.ai_suspended) {
        synui_spawn(s->config.game_ai_start_cmd);
        s->game.ai_suspended = 0;
        /* The poller reconnects on its own once synapd is back up (it retries),
         * so it can be restarted immediately — synapd is still reloading its
         * model at this point and will not answer for a few seconds. */
        synmon_start(s);
        wlr_log(WLR_INFO, "synui: game: restoring synapd — `%s`",
                s->config.game_ai_start_cmd);
    }

    /* Rearm the idle stages from now, not from whenever the game started. */
    power_notify_activity(s);

    game_publish(s);
}

/* The single decision point. Cheap and idempotent, so it is safe to call from
 * every fullscreen change, map, and unmap. */
void game_reevaluate(syn_server_t *s)
{
    if (!s->config.game_mode) {
        if (s->game.active) game_leave(s);
        return;
    }

    int want;
    syn_view_t *v = NULL;
    if (s->game.forced > 0) {
        want = 1;                       /* Super+G forced it on */
    } else if (s->game.forced < 0) {
        want = 0;                       /* Super+G forced it off */
    } else {
        v = game_find_view(s);
        want = (v != NULL);
    }

    if (want == s->game.active) return;
    if (want) game_enter(s, v);
    else      game_leave(s);
}

/* Super+G. Cycles auto → forced-on → forced-off → auto, so a user can both
 * force game mode for a window we failed to detect and force it *off* for a
 * false positive without editing the config. */
void game_toggle(syn_server_t *s)
{
    if (s->game.forced == 0)      s->game.forced = s->game.active ? -1 : 1;
    else if (s->game.forced > 0)  s->game.forced = -1;
    else                          s->game.forced = 0;

    wlr_log(WLR_INFO, "synui: game: manual override → %s",
            s->game.forced > 0 ? "forced ON" :
            s->game.forced < 0 ? "forced OFF" : "auto");
    game_reevaluate(s);
    /* reevaluate only publishes when active flips; the override can change
     * `mode` on its own (auto → forced-off with no game up), and the bar's
     * tooltip shows it. */
    game_publish(s);
}

/* Compositor shutdown. If we stopped synapd, start it again — otherwise a synui
 * that exits (or crashes) mid-game leaves the box with no AI and no clue why. */
void game_finish(syn_server_t *s)
{
    if (s->game.ai_suspended) {
        synui_spawn(s->config.game_ai_start_cmd);
        s->game.ai_suspended = 0;
        wlr_log(WLR_INFO, "synui: game: shutting down with synapd suspended "
                          "— restoring it");
    }
    s->game.active = 0;
    game_publish(s);
}
