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
 * ...and one hole that signal has: a gamescope launched from a Wayland session
 * is a Wayland-native client, because it runs the game on a nested Xwayland of
 * its own that synui never sees. Every Steam title with `gamescope -f --
 * %command%` in its launch options was therefore invisible to game mode. Those
 * are named in game_include; see synui.h for why an allow-list and not "any
 * fullscreen Wayland client".
 *
 * Placement. A game also gets put on the main screen (game_output) — the
 * clients cannot be trusted to pick, and the failure is loud: a game that
 * opens on the wrong monitor every time. game_output_for() has the details.
 *
 * What it does while a game is up:
 *
 *   - Tells synapd the GPU is wanted, and tells it again when the game leaves.
 *
 *     ⛔ IT NO LONGER STOPS THE DAEMON. It used to, and the reason written here
 *     was that synapd "has no unload/sleep IPC — synapd.h offers only RELOAD
 *     and SHUTDOWN". That stopped being true when SLEEP was added for the
 *     suspend hook, and this comment outlived the fact by long enough that the
 *     sledgehammer looked like the considered choice.
 *
 *     The cost of it was never only the chat model. `systemctl stop` takes the
 *     whole daemon, including the retrieval embedder — a separate 274 MB model
 *     that has nothing to do with the GPU pressure — so chibi's memory went
 *     dark for the length of every game. synapd's own source carried three
 *     comments promising that could not happen.
 *
 *     Now it is one message (SYN_MSG_DEMAND, "high"/"normal") and synapd
 *     decides what to do about it: it re-fits the model to the VRAM actually
 *     left and keeps answering, slower, from RAM. What it must NOT be handed
 *     is a layer count — the compositor knows a game started; it does not know
 *     how big the model is or what card this is.
 *
 *   - Leaves the synapd poller running, because synapd is still there.
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
#include <dirent.h>

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
    /*
     * ⚠ THREE STATES, AND "yielded" IS THE ONE THE DAEMON CONFIRMED. synapd is
     * no longer stopped for a game — it is told the GPU is wanted and re-fits
     * its model to what is left — so the old "suspended" value, and the
     * indicator's check that synapd had actually gone away, now describe a
     * mechanism that is not there. A tooltip warning "synapd STILL RUNNING"
     * would fire on every game and be exactly backwards.
     */
    fprintf(f, "ai=%s\n", s->game.ai_ack ? "yielded" :
                          s->game.ai_suspended ? "asked" :
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

/* Is this a Wayland-NATIVE client that is nonetheless a game wrapper?
 * Same matching rule as the exclusion list, opposite sense — see the
 * game_include comment in synui.h for why this has to be an allow-list. */
static int game_included(const syn_config_t *cfg, const char *app_id)
{
    if (!app_id) return 0;
    for (int i = 0; i < cfg->game_include_count; i++)
        if (strcasestr(app_id, cfg->game_include[i])) return 1;
    return 0;
}

/* The shared definition of "this is a game". Kept silent so it can be called
 * from the placement path as well as the detector; the logging that used to
 * live here is in game_find_view(), which runs once per decision. */
int game_view_is_game(syn_server_t *s, syn_view_t *view)
{
    if (!view || !view->mapped || !view->fullscreen) return 0;

    const char *aid = view_app_id(view);

    if (!view->is_xwayland) {
        /* A Wayland-native client is an ordinary desktop app unless it is a
         * named wrapper. Note the exclusion list still applies on top, so a
         * wrapper can be un-named again without editing the include list. */
        return game_included(&s->config, aid) && !game_excluded(&s->config, aid);
    }

    /* A NULL/empty WM_CLASS cannot match an exclusion, so it stays a game: the
     * fullscreen-X11 evidence stands on its own and the class is only ever
     * used to rule things out. */
    if (!aid || !*aid) return 1;
    return !game_excluded(&s->config, aid);
}

/* The first mapped, fullscreen view that counts as a game — or NULL.
 * Views live per-workspace, so a game on an inactive workspace still counts:
 * it is still rendering and still holding the GPU. */
static syn_view_t *game_find_view(syn_server_t *s)
{
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            if (!game_view_is_game(s, v)) continue;
            /* An unexpected trigger is exactly what a user would want to see,
             * and a classless client is the one case decided by absence of
             * evidence rather than by a match. */
            const char *aid = view_app_id(v);
            if (v->is_xwayland && (!aid || !*aid))
                wlr_log(WLR_INFO, "synui: game: fullscreen X11 client with no "
                                  "WM_CLASS — treating as a game");
            return v;
        }
    }
    return NULL;
}

/* Which monitor a game belongs on.
 *
 * The client's own answer is not usable. An X11 game picks by RandR order
 * unless something marks a primary (see xwayland_apply_primary), and a
 * gamescope on the Wayland backend names an output of its own choosing that
 * neither `-O/--prefer-output` nor the X11 primary flag budges — measured
 * 2026-08-08: with DP-3 both primary AND focused, `gamescope -O DP-3 -f`
 * fullscreened onto DP-2 anyway. So the compositor decides, or nobody does.
 *
 * Returns NULL for "not our call", which is every non-game and the explicit
 * GAME_OUT_ASK setting for anyone who wants the old behaviour back. */
syn_output_t *game_output_for(syn_server_t *s, syn_view_t *view)
{
    if (!s->config.game_mode) return NULL;
    if (s->config.game_output == GAME_OUT_ASK) return NULL;
    /* forced < 0 is "no game mode right now", and that has to include the
     * placement: a user who turned game mode off for a false positive should
     * get their window back where they put it. forced > 0 does NOT force
     * placement, because the view it is applied to may be any window. */
    if (s->game.forced < 0) return NULL;
    if (!game_view_is_game(s, view)) return NULL;

    syn_output_t *o = s->config.game_output == GAME_OUT_FOCUSED
                    ? server_focused_output(s)
                    : server_primary_output(s);
    /* server_primary_output falls back to the largest enabled output, so a NULL
     * here means there are no outputs at all — nothing to place onto. */
    return o;
}

/* Repaint every output now. Toggling the post-process pass changes the whole
 * render path, so waiting for incidental damage would leave the old path on
 * screen until something else moved. */
static void game_repaint_all(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);
}

/* Is a wallpaper engine actually running?
 *
 * Asked before stopping it, and the answer is what gates the restore on exit:
 * `synui-wpengine restore` re-applies the SAVED state, so running it for a user
 * who had no wallpaper engine up would switch one on that they never had. Only
 * pause what was actually playing.
 *
 * /proc rather than the control script: this runs on every fullscreen change,
 * and spawning a shell to ask a yes/no question is not something to do on that
 * path. comm is truncated to 15 chars by the kernel, so the prefix is the whole
 * name available — hence a prefix compare, not an equality one. */
static int game_wpengine_running(void)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;

    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;

        char path[64], comm[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;                       /* raced with exit; fine */
        if (fgets(comm, sizeof(comm), f)) {
            comm[strcspn(comm, "\r\n")] = '\0';
            if (strncmp(comm, "linux-wallpaper", 15) == 0) found = 1;
        }
        fclose(f);
    }
    closedir(d);
    return found;
}

static void game_enter(syn_server_t *s, syn_view_t *v)
{
    const char *aid = (v && view_app_id(v)) ? view_app_id(v) : "unknown";
    snprintf(s->game.app, sizeof(s->game.app), "%s", aid);
    s->game.active = 1;
    wlr_log(WLR_INFO, "synui: game: ON (%s)", s->game.app);

    if (s->config.game_suspend_ai && !s->game.ai_suspended) {
        s->game.ai_ack       = (ai_notify_demand(1) == 0);
        s->game.ai_suspended = 1;

        /*
         * ⚠ THE POLLER STAYS UP, because synapd does. It was stopped here only
         * because the daemon was about to be, and a poller reconnecting to a
         * socket that is going away does nothing but log.
         *
         * The command below is empty by default and exists only so a box that
         * SET it keeps the behaviour it configured — see game_ai_stop_cmd in
         * config.c. Nothing runs when it is empty, which is the normal case.
         */
        if (s->config.game_ai_stop_cmd[0]) {
            synmon_stop(s);
            synui_spawn(s->config.game_ai_stop_cmd);
            wlr_log(WLR_INFO, "synui: game: running the configured AI stop "
                    "command — `%s`", s->config.game_ai_stop_cmd);
        }
    }

    /* Drop the post-process pass, which is what actually costs a game frames.
     *
     * With it on, effects.c renders the scene into an offscreen swapchain and
     * forces whole-output damage every frame, then runs a fullscreen shader
     * over the result — so the game's buffer can never be handed to direct
     * scanout, which is the whole point of a fullscreen client. Nothing is lost
     * visually: CRT warp, scanlines and vignette are behind an opaque game.
     *
     * The prior value is saved rather than assumed, so a user who had effects
     * off already does not come out of a game with them on. */
    if (s->config.game_drop_effects && !s->game.effects_dropped &&
        s->config.effects) {
        s->game.effects_saved   = s->config.effects;
        s->config.effects       = 0;
        s->game.effects_dropped = 1;
        game_repaint_all(s);
        wlr_log(WLR_INFO, "synui: game: post-process pass off "
                          "(fullscreen client can reach direct scanout)");
    }

    /* Wallpaper Engine renders continuously into a surface an opaque
     * fullscreen game completely covers, so every frame of it is waste — and
     * it leaks RSS per output while it does, which a long session accumulates.
     * `off all` hands the background back to synui; `restore` re-applies the
     * saved state on the way out.
     *
     * Only stopped if something is actually running: `restore` on exit would
     * otherwise START a wallpaper for a user who had none, which is a setting
     * changed behind their back rather than one put back. */
    if (s->config.game_pause_wallpaper && !s->game.wallpaper_paused &&
        game_wpengine_running()) {
        synui_spawn(s->config.game_wp_stop_cmd);
        s->game.wallpaper_paused = 1;
        wlr_log(WLR_INFO, "synui: game: pausing wallpaper engine — `%s`",
                s->config.game_wp_stop_cmd);
    }

    /* The bar is a few hundred MB of QML that a fullscreen game hides. Off by
     * default: the CPU saving is negligible and the restart is visible when you
     * alt-tab out, so this is only worth it if the memory is what is tight. */
    if (s->config.game_stop_bar && !s->game.bar_stopped) {
        synui_spawn(s->config.game_bar_stop_cmd);
        s->game.bar_stopped = 1;
        wlr_log(WLR_INFO, "synui: game: stopping the bar — `%s`",
                s->config.game_bar_stop_cmd);
    }

    /* Kernel-side event construction. Measured saving is near zero — the probes
     * still trap, this only skips building and queueing the event — and it
     * costs synguard its event stream for the duration, so it is opt-in. */
    if (s->config.game_quiet_kmod && !s->game.kmod_quieted) {
        synui_spawn(s->config.game_kmod_quiet_cmd);
        s->game.kmod_quieted = 1;
        wlr_log(WLR_INFO, "synui: game: quieting synapse_kmod events — `%s`",
                s->config.game_kmod_quiet_cmd);
    }

    /* Re-run the idle arming with game mode now set: power_arm() sees it and
     * leaves every stage disarmed. Also undoes a dim that already landed. */
    if (s->config.game_inhibit_idle) power_notify_activity(s);

    game_publish(s);
}

static void game_leave_cancel(syn_server_t *s);

static void game_leave(syn_server_t *s)
{
    /* However this was reached — the grace expiring, the master switch, or
     * shutdown — the pending leave has happened and must not fire again. */
    game_leave_cancel(s);

    s->game.active = 0;
    s->game.app[0] = '\0';
    wlr_log(WLR_INFO, "synui: game: OFF");

    if (s->game.ai_suspended) {
        ai_notify_demand(0);
        s->game.ai_suspended = 0;
        s->game.ai_ack       = 0;
        wlr_log(WLR_INFO, "synui: game: synapd may have the GPU back");

        /* Only if this box configured the old command pair — see above. The
         * poller reconnects on its own once synapd is up again (it retries),
         * so it can be restarted immediately even though the model is still
         * loading and will not answer for a few seconds. */
        if (s->config.game_ai_start_cmd[0]) {
            synui_spawn(s->config.game_ai_start_cmd);
            synmon_start(s);
            wlr_log(WLR_INFO, "synui: game: running the configured AI start "
                    "command — `%s`", s->config.game_ai_start_cmd);
        }
    }

    /* Undo, each gated on "we are the ones who did it". A setting the user
     * changed mid-game is theirs; only our own changes get reverted. */
    if (s->game.effects_dropped) {
        s->config.effects       = s->game.effects_saved;
        s->game.effects_dropped = 0;
        game_repaint_all(s);
        wlr_log(WLR_INFO, "synui: game: post-process pass restored");
    }
    if (s->game.wallpaper_paused) {
        synui_spawn(s->config.game_wp_start_cmd);
        s->game.wallpaper_paused = 0;
        wlr_log(WLR_INFO, "synui: game: restoring wallpaper engine — `%s`",
                s->config.game_wp_start_cmd);
    }
    if (s->game.bar_stopped) {
        synui_spawn(s->config.game_bar_start_cmd);
        s->game.bar_stopped = 0;
        wlr_log(WLR_INFO, "synui: game: restarting the bar — `%s`",
                s->config.game_bar_start_cmd);
    }
    if (s->game.kmod_quieted) {
        synui_spawn(s->config.game_kmod_restore_cmd);
        s->game.kmod_quieted = 0;
        wlr_log(WLR_INFO, "synui: game: restoring synapse_kmod events — `%s`",
                s->config.game_kmod_restore_cmd);
    }

    /* Rearm the idle stages from now, not from whenever the game started. */
    power_notify_activity(s);

    game_publish(s);
}

/*
 * ⚠ A GAME THAT HAS MINIMISED ITSELF IS STILL A GAME.
 *
 * An exclusive-fullscreen X11 title unmaps its window every time it loses
 * focus. Cyberpunk 2077 does it on every single Alt-Tab — measured 2026-08-26,
 * one `X11 window mapped` line in the journal for every focus flip. xw_unmap()
 * takes the view out of its workspace list with it, so game_find_view()
 * answers NULL, the grace timer runs out, and game mode LEAVES: synapd
 * restarts, kmod events come back, and the bar is relaunched — a whole desktop
 * rebuilt because somebody looked at their browser for six seconds. Tabbing
 * back re-enters game mode and tears it all down again. Twice in three and a
 * half minutes on the measured session, which is what "it does not come back
 * when I Alt-Tab out" looks like from the inside.
 *
 * The window has not gone anywhere; only its buffer has. So while game mode is
 * ALREADY engaged, an unmapped-but-alive fullscreen X11 view counts as the
 * game still being there.
 *
 * ⚠ Deliberately NOT an entry condition — a window nobody can see must never
 * turn game mode on, so this answers NULL unless game mode is already active.
 * And deliberately keyed on the X surface still existing: a game that really
 * quit stops counting the moment its window is destroyed, which is why
 * xw_destroy() asks again. s->xw_views is the only list an unmapped X11 view
 * is still on.
 */
syn_view_t *game_minimized_view(syn_server_t *s)
{
    if (!s->game.active) return NULL;

    syn_view_t *v;
    wl_list_for_each(v, &s->xw_views, xw_link) {
        if (v->mapped || v->override_redirect) continue;
        if (!v->xsurface || !v->fullscreen) continue;
        /* The same exclusion list a mapped game answers to — an app the user
         * has named as not-a-game does not become one by minimising. */
        const char *aid = view_app_id(v);
        if (aid && *aid && game_excluded(&s->config, aid)) continue;
        return v;
    }
    return NULL;
}

/* ── Leaving is deferred, entering is not ─────────────────────
 *
 * See syn_game_t.leave_timer. A game appearing is evidence; a game
 * disappearing is only a guess until the gap has lasted longer than the
 * gaps a running game makes on its own.
 */
static void game_leave_cancel(syn_server_t *s)
{
    if (!s->game.leave_timer) return;
    wl_event_source_timer_update(s->game.leave_timer, 0);
}

/* The grace ran out. Ask again rather than trusting the answer that armed it:
 * a game that came back and went away again during the wait would otherwise be
 * left in whichever state the older question found. */
static int game_leave_fire(void *data)
{
    syn_server_t *s = data;
    if (!s->game.active) return 0;
    if (s->game.forced > 0) return 0;
    if (s->config.game_mode && s->game.forced == 0 &&
        (game_find_view(s) || game_minimized_view(s))) {
        /* Still there. Nothing to do — the timer is one-shot and disarmed. */
        wlr_log(WLR_DEBUG, "synui: game: grace expired but a game is up");
        return 0;
    }
    wlr_log(WLR_INFO, "synui: game: no game for %d ms — leaving",
            s->config.game_leave_grace_ms);
    game_leave(s);
    return 0;
}

static void game_leave_arm(syn_server_t *s)
{
    if (s->config.game_leave_grace_ms <= 0) { game_leave(s); return; }

    if (!s->game.leave_timer) {
        struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
        s->game.leave_timer = wl_event_loop_add_timer(loop, game_leave_fire, s);
        /* No timer means no grace, and that is the old behaviour rather than a
         * game mode that never turns off. */
        if (!s->game.leave_timer) { game_leave(s); return; }
    }
    wl_event_source_timer_update(s->game.leave_timer,
                                 s->config.game_leave_grace_ms);
    wlr_log(WLR_DEBUG, "synui: game: no game right now — holding for %d ms",
            s->config.game_leave_grace_ms);
}

/* The single decision point. Cheap and idempotent, so it is safe to call from
 * every fullscreen change, map, and unmap. */
void game_reevaluate(syn_server_t *s)
{
    if (!s->config.game_mode) {
        /* The master switch is not a gap in the evidence: it is the user (or
         * the control panel) saying no. Off now, no grace. */
        game_leave_cancel(s);
        if (s->game.active) game_leave(s);
        return;
    }

    int want;
    int forced = 0;
    syn_view_t *v = NULL;
    if (s->game.forced > 0) {
        want = 1; forced = 1;           /* Super+G forced it on */
    } else if (s->game.forced < 0) {
        want = 0; forced = 1;           /* Super+G forced it off */
    } else {
        v = game_find_view(s);
        /* Answers only while game mode is already engaged, so a minimised
         * window can keep game mode on but can never turn it on. */
        if (!v) v = game_minimized_view(s);
        want = (v != NULL);
    }

    if (want) {
        /* A game is up, so whatever gap was being waited out is over. This
         * runs even when game mode is already active — that IS the common
         * case, and it is the whole point: the next fullscreen surface
         * arriving cancels the leave the previous one's unmap armed. */
        game_leave_cancel(s);
        if (!s->game.active) game_enter(s, v);
        return;
    }

    if (!s->game.active) return;
    /* Super+G forced off is a decision, not an absence — it takes effect now. */
    if (forced) { game_leave_cancel(s); game_leave(s); return; }
    game_leave_arm(s);
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

/* Compositor shutdown. Put the AI's GPU back, otherwise a synui that exits (or
 * crashes) mid-game leaves synapd shed onto the CPU for the rest of the session
 * with nothing left to explain why the assistant got slow. */
void game_finish(syn_server_t *s)
{
    /* The event loop is going away with the compositor, so the grace timer has
     * to go first: a source left behind outlives the loop it was added to. */
    if (s->game.leave_timer) {
        wl_event_source_remove(s->game.leave_timer);
        s->game.leave_timer = NULL;
    }

    if (s->game.ai_suspended) {
        /*
         * ⚠ THE HINT HAS TO BE WITHDRAWN EXPLICITLY, because it now outlives
         * us. Stopping the unit was self-correcting in the worst case — a
         * systemd unit comes back — but a demand flag set inside a daemon that
         * keeps running does not, and synapd would sit at the game floor
         * indefinitely with the game long gone.
         */
        ai_notify_demand(0);
        s->game.ai_suspended = 0;
        wlr_log(WLR_INFO, "synui: game: shutting down mid-game — synapd may "
                          "have the GPU back");
        if (s->config.game_ai_start_cmd[0])
            synui_spawn(s->config.game_ai_start_cmd);
    }
    /* The in-process ones do not matter here — synui is exiting and takes
     * config.effects with it. These are separate PROCESSES, and leaving a
     * desktop with no wallpaper, no bar, or a silenced security module because
     * the compositor died mid-game is the same failure the synapd restore above
     * exists to prevent. */
    if (s->game.wallpaper_paused) {
        synui_spawn(s->config.game_wp_start_cmd);
        s->game.wallpaper_paused = 0;
        wlr_log(WLR_INFO, "synui: game: shutting down — restoring the wallpaper");
    }
    if (s->game.bar_stopped) {
        synui_spawn(s->config.game_bar_start_cmd);
        s->game.bar_stopped = 0;
        wlr_log(WLR_INFO, "synui: game: shutting down — restarting the bar");
    }
    if (s->game.kmod_quieted) {
        synui_spawn(s->config.game_kmod_restore_cmd);
        s->game.kmod_quieted = 0;
        wlr_log(WLR_INFO, "synui: game: shutting down — restoring kmod events");
    }

    s->game.active = 0;
    game_publish(s);
}

/* The rectangle the game is actually DRAWN in — NOT the view box.
 * view_fullscreen_rescale() fits a sub-native surface inside the fullscreen
 * frame and centres it, so the frame can carry letterbox bars that belong to
 * no surface at all, and those bars are the whole reason game_confine_rect()
 * exists. One owner for the measurement, in xwayland.c beside the code that
 * sets the size it reads. */
#define game_content_box(v, out)  view_scaled_content_box((v), (out))

/*
 * Which rectangle holds the pointer: the game's own surface, clipped to the
 * screen it is on.
 *
 * ⚠ THE SURFACE, NOT THE OUTPUT — AND THE LETTERBOX BARS ARE THE REASON.
 *
 * 510 clamped to the output so the bars stayed reachable. That was backwards.
 * The bars belong to no surface, so a pointer that reaches one makes
 * surface_at() answer NULL; pointer_update_focus() then clears pointer focus
 * and calls constraints_focus_surface(s, NULL), which deactivates the game's
 * pointer lock — and deactivating a ONESHOT constraint DESTROYS it. The client
 * has to ask again, and a game that asked once at startup never does, so
 * mouse-look is dead for the rest of the session.
 *
 * Measured on Cyberpunk 2077 (steam_app_1091500, 2026-08-26): the lock held the
 * cursor still at one point for 19 s, released the instant the pointer crossed
 * into a bar, and the cursor free-roamed the screen from then on, pinning at
 * the old output clamp's corner. Reported as three different bugs — "drifts to
 * another monitor", "can't move right", "moves a few inches then releases" —
 * which are one bug seen at three moments.
 *
 * A game that fills its output exactly gets the same rectangle either way.
 *
 * `content` may be a zero box, meaning nothing could be measured; the output is
 * then the best answer available and the old behaviour is what happens. The
 * intersection is not paranoia: a client sets its own buffer dest size, and an
 * escape hatch is worth nothing if the confine can follow it off the screen.
 */
int game_confine_rect(const struct wlr_box *out, const struct wlr_box *content,
                      struct wlr_box *dst)
{
    if (!out || !dst || out->width <= 0 || out->height <= 0) return 0;

    if (content && content->width > 0 && content->height > 0) {
        struct wlr_box hit;
        if (wlr_box_intersection(&hit, content, out) &&
            hit.width > 0 && hit.height > 0) {
            *dst = hit;
            return 1;
        }
    }

    *dst = *out;
    return 1;
}

/*
 * Which point of a fullscreen window's PICTURE answers for a point in its BOX.
 *
 * The box is what the window covers; the picture is what is drawn in it, and
 * view_fullscreen_rescale() lets the two differ — a sub-native surface is
 * fitted and centred, leaving letterbox bars nothing is painted in. The scene
 * answers NULL in a bar, and NULL ends mouse capture.
 *
 * ⚠ AND THE TWO DIFFER EVEN WHEN THEY DO NOT. A picture that fills its frame
 * still lost pointer focus on its last column and last row — measured on
 * Cyberpunk 2077 filling DP-3 exactly, x 3639 and y 2519 and nowhere else,
 * fourteen times in twelve seconds. Those are the two lines game_confine_cursor()
 * parks the cursor on. So ownership is decided by the BOX alone: a fullscreen
 * window owns every pixel of it, and the picture only decides WHICH of its own
 * points answers.
 *
 * Answers 0 when the point is outside the box — someone else's problem — and
 * otherwise fills cx/cy with a point that is inside the picture.
 */
int game_fullscreen_owns_point(const struct wlr_box *box,
                               const struct wlr_box *content,
                               double lx, double ly, double *cx, double *cy)
{
    if (!box || !content || !cx || !cy) return 0;
    if (box->width <= 0 || box->height <= 0) return 0;
    if (content->width <= 0 || content->height <= 0) return 0;

    if (lx < box->x || lx >= box->x + box->width ||
        ly < box->y || ly >= box->y + box->height) return 0;

    /* One short of the far edge, the same reckoning game_confine_cursor() uses:
     * c.x + c.width is the first column OUTSIDE the picture. */
    const double rx = content->x + content->width  - 1;
    const double by = content->y + content->height - 1;
    *cx = lx < content->x ? content->x : (lx > rx ? rx : lx);
    *cy = ly < content->y ? content->y : (ly > by ? by : ly);
    return 1;
}

/* ── Keeping the pointer on the game's screen ───────────────
 *
 * A fullscreen game on a multi-monitor desk is supposed to capture the mouse,
 * and on Wayland that is the client's job: Wine/Xwayland asks for a
 * zwp_locked_pointer or zwp_confined_pointer and constraints.c honours it.
 *
 * MEASURED 2026-08-26, Cyberpunk 2077 under proton-cachyos, three outputs:
 * across 16010 samples in 59 s with steam_app_1091500 holding focus, the
 * cursor ranged over x 0..3639, y 75..2602 — every one of the three monitors.
 * The request never arrives, so there is nothing for constraints.c to honour
 * and no amount of fixing it there can help. (Once the pointer is off the
 * game, Steam and the game then trade focus back and forth — 34 flips in
 * three minutes — which is the "can't even keep the window focused" half of
 * the same report.)
 *
 * So the compositor answers for itself. Game mode already knows a fullscreen
 * game is running and which output it is on; that is enough to hold the
 * pointer there without asking the client for permission, which is what
 * gamescope does and what every user means by "game mode".
 *
 * Only while the game HOLDS FOCUS. Alt-Tab, or any bind that moves focus,
 * frees the pointer immediately and clicking back into the game takes it
 * again — so the mouse can never be stuck on one monitor with no way out,
 * which matters most on the desk this was measured on (three screens, a
 * terminal on one of them).
 *
 * Answers 0 when the pointer is nobody's business but the user's; otherwise
 * fills `box` with the layout rectangle to hold it inside. */
int game_pointer_box(syn_server_t *s, struct wlr_box *box)
{
    if (!s || !box) return 0;
    if (!s->config.game_mode || !s->config.game_confine_pointer) return 0;
    if (!s->game.active) return 0;

    syn_view_t *v = game_find_view(s);
    if (!v) return 0;
    /* The escape hatch, and the whole reason this is safe to default on. */
    if (s->focused_view != v) return 0;

    if (!v->output) return 0;

    struct wlr_box ob;
    output_box_of(s, v->output, &ob);
    if (ob.width <= 0 || ob.height <= 0) return 0;

    struct wlr_box cb;
    if (!game_content_box(v, &cb)) cb = (struct wlr_box){ 0, 0, 0, 0 };
    return game_confine_rect(&ob, &cb, box);
}

/* Is a fullscreen game living on this output right now?
 *
 * Deliberately NOT game_pointer_box()'s question. That one asks whether to
 * hold the pointer and so requires the game to hold FOCUS — Alt-Tab is its
 * escape hatch. This one asks whether the screen is covered by a game, which
 * is just as true of a game the user has tabbed away from: the opaque
 * fullscreen surface is still there, still covering everything under it. A
 * caller that skips work because nothing on this output can be seen wants
 * this one; a caller that changes what the USER's input does wants the other.
 */
int game_owns_output(syn_server_t *s, syn_output_t *o)
{
    if (!s || !o) return 0;
    if (!s->config.game_mode || !s->game.active) return 0;
    syn_view_t *v = game_find_view(s);
    return v && v->output == o;
}

/* Pull the cursor back onto the game's screen. Called from the motion path
 * after the cursor has moved, so it clamps a position rather than a delta —
 * wlr_cursor_move maps the delta through the device's own output mapping and
 * does not promise to apply it verbatim, so clamping the delta beforehand
 * would be clamping a number the cursor never used. */
void game_confine_cursor(syn_server_t *s)
{
    struct wlr_box b;
    if (!game_pointer_box(s, &b)) return;

    double x = s->cursor->x, y = s->cursor->y;
    /* width - 1, not width: box.x + box.width is the first column OUTSIDE the
     * rectangle — the next output when this is a screen, the letterbox bar when
     * it is the game's surface — and a cursor parked exactly there has already
     * left the thing it is meant to be held inside. */
    double cx = x < b.x ? b.x : (x > b.x + b.width  - 1 ? b.x + b.width  - 1 : x);
    double cy = y < b.y ? b.y : (y > b.y + b.height - 1 ? b.y + b.height - 1 : y);
    if (cx == x && cy == y) return;

    wlr_cursor_warp_closest(s->cursor, NULL, cx, cy);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;
}

/* ── The diagnostic probe behind `synctl pointer` ───────────
 *
 * Everything above decides; this only reports. It exists because the whole
 * pointer story — does the game hold a lock, does it merely have one on file,
 * which rectangle is holding the cursor — is invisible from outside the
 * compositor, and the three sessions this file's comments record were each
 * spent inferring it from a cursor sample. A cursor sample cannot tell a
 * working lock from a game that never asked for one; this can.
 *
 * SILENT and cheap, because a probe is sampled in a loop: it repeats
 * game_find_view()'s walk rather than calling it, whose one log line would
 * otherwise print several times a second.
 */
syn_view_t *game_probe_view(syn_server_t *s)
{
    if (!s) return NULL;
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link)
            if (game_view_is_game(s, v)) return v;
    }
    return NULL;
}

