/*
 * power.c — idle power saving, and the panel that configures it
 *
 * Four independent stages fire after the seat has been idle for their
 * configured number of seconds (0 = never):
 *
 *   dim       fade a translucent black overlay over the whole layout
 *   blank     DPMS the outputs off
 *   lock      run power_lock_cmd (swaylock)
 *   suspend   run power_suspend_cmd (systemctl suspend)
 *
 * The stages are independent rather than cumulative: each is measured from
 * the last input event, so "dim at 240, blank at 600" means exactly that,
 * not "blank 600s after dimming". Any input event undoes whatever fired and
 * rearms every stage from zero.
 *
 * This replaces the old swayidle + synui-idle-wrapper.sh setup. Doing it in
 * the compositor means the Super+P panel can retune timeouts live with no
 * daemon restart, and it removes the failure mode that actually bit us:
 * swayidle exits 0 when its Wayland connection drops, so a synui restart
 * left the session with no idle policy at all and Restart=on-failure never
 * noticed.
 *
 * Idle inhibitors (zwp_idle_inhibit_manager_v1 — synui-media-inhibit creates
 * one whenever audio is playing) disarm every stage while any are held.
 *
 * Closing a laptop lid is handled here too, though it is not an idle stage:
 * it is an event, not a timeout, and it runs its action at once. See
 * power_lid_set() at the bottom.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>    /* /sys/class/power_supply — see power_on_ac */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp — connector names are not case-normalised */
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* How dark the dim stage goes. Dark enough to read as "about to blank",
 * light enough that you can still see what you were doing. */
#define POWER_DIM_ALPHA 0.65f

/* Timeouts the panel's Left/Right steps through. 0 first, so stepping left
 * off the bottom lands on "never" rather than wrapping to three hours. */
static const int power_ladder[] = {
    0, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1800, 2700, 3600, 7200, 10800,
};
static const int power_ladder_len =
    (int)(sizeof(power_ladder) / sizeof(power_ladder[0]));

/* Config-file, panel and power.state spelling of each lid action. Names, not
 * indices, in the state file: the enum will grow (hibernate, screen off) and a
 * saved 3 must not silently become a different action after it does. */
const char *const syn_lid_action_names[SYN_LID_ACTION_COUNT] = {
    "system", "ignore", "blank", "lock", "suspend",
};

int lid_action_from_name(const char *name)
{
    for (int i = 0; i < SYN_LID_ACTION_COUNT; i++)
        if (strcmp(name, syn_lid_action_names[i]) == 0) return i;
    return -1;
}

/* ── Config field <-> panel row ──────────────────────────── */

/* The idle timeout a row edits. POWER_ROW_ENABLED toggles the master switch
 * and the lid rows pick an action, so both map to NULL and callers must
 * check — row_action_field() covers the latter. */
static int *row_field(syn_server_t *s, int row)
{
    switch (row) {
    case POWER_ROW_DIM:     return &s->config.power_dim;
    case POWER_ROW_BLANK:   return &s->config.power_blank;
    case POWER_ROW_LOCK:    return &s->config.power_lock;
    case POWER_ROW_SUSPEND: return &s->config.power_suspend;
    default:                return NULL;
    }
}

/* The syn_lid_action_t a row edits, or NULL if the row is not a lid row. */
static int *row_action_field(syn_server_t *s, int row)
{
    switch (row) {
    case POWER_ROW_LID:        return &s->config.lid_close_action;
    case POWER_ROW_LID_AC:     return &s->config.lid_close_ac_action;
    case POWER_ROW_LID_DOCKED: return &s->config.lid_close_docked_action;
    default:                   return NULL;
    }
}

static const char *row_label(int row)
{
    switch (row) {
    case POWER_ROW_ENABLED:    return "Power saving";
    case POWER_ROW_DIM:        return "Dim screen";
    case POWER_ROW_BLANK:      return "Blank displays";
    case POWER_ROW_LOCK:       return "Lock session";
    case POWER_ROW_SUSPEND:    return "Suspend system";
    case POWER_ROW_LID:        return "Lid closed (battery)";
    case POWER_ROW_LID_AC:     return "Lid closed (plugged in)";
    case POWER_ROW_LID_DOCKED: return "Lid closed (docked)";
    default:                   return "?";
    }
}

/* "never" / "45s" / "5m" / "1m30s" / "3h" — compact enough for a table column.
 *
 * Whole hours get an hours unit rather than "180m": the top of the ladder was
 * already showing two hours as "120m", which reads as a misconfiguration more
 * than a setting, and it only gets worse the further the ladder goes. */
static void power_format_timeout(int secs, char *buf, size_t n)
{
    if (secs <= 0)             snprintf(buf, n, "never");
    else if (secs < 60)        snprintf(buf, n, "%ds", secs);
    else if (secs % 3600 == 0) snprintf(buf, n, "%dh", secs / 3600);
    else if (secs % 60 == 0)   snprintf(buf, n, "%dm", secs / 60);
    else                       snprintf(buf, n, "%dm%ds", secs / 60, secs % 60);
}

/* ── Dim overlay ─────────────────────────────────────────── */

static void power_set_dim(syn_server_t *s, bool on)
{
    if (on == (bool)s->power.dimmed) return;
    s->power.dimmed = on;
    wlr_log(WLR_INFO, "synui: power: dim %s", on ? "on" : "off");

    if (!on) {
        if (s->power_ui.dim)
            wlr_scene_node_set_enabled(&s->power_ui.dim->node, false);
        return;
    }

    /* Cover the whole output layout, not one output: the overlay has to
     * follow a hotplug that grew the layout since it was last shown. */
    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, NULL, &box);

    float color[4] = { 0.0f, 0.0f, 0.0f, POWER_DIM_ALPHA };
    if (!s->power_ui.dim)
        s->power_ui.dim = wlr_scene_rect_create(s->power_ui.dim_tree,
                                                box.width, box.height, color);
    else
        wlr_scene_rect_set_size(s->power_ui.dim, box.width, box.height);

    wlr_scene_node_set_position(&s->power_ui.dim->node, box.x, box.y);
    wlr_scene_node_set_enabled(&s->power_ui.dim->node, true);
    wlr_scene_node_raise_to_top(&s->power_ui.dim_tree->node);
}

/* ── Blank (DPMS) ────────────────────────────────────────── */

/* The built-in laptop panel, by connector type. wlroots names a DRM output
 * after its connector, so this is the same test every other compositor uses to
 * tell a lid from a monitor. A machine with none (a desktop, or any non-DRM
 * backend) simply never matches, and the lid rows then have nothing to act on.
 *
 * Prefix, not exact: the connector is eDP-1 / LVDS-1 / DSI-1, and the index is
 * not always 1 on a machine with more than one internal panel. */
static bool output_is_internal(struct wlr_output *o)
{
    static const char *const internal[] = { "eDP-", "LVDS-", "DSI-" };
    for (size_t i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
        if (strncasecmp(o->name, internal[i], strlen(internal[i])) == 0)
            return true;
    return false;
}

bool power_docked(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (!output_is_internal(o->wlr_output)) return true;
    return false;
}

/* ── Mains power ─────────────────────────────────────────── */

/* Overridden only by tests/lid_test.c, which points it at a scratch tree it
 * builds itself — there is no other way to run the on-battery branch on a
 * machine that is plugged in. Compile-time, so the shipped binary has no way
 * to be aimed anywhere but the real sysfs. */
#ifndef SYNUI_POWER_SUPPLY_DIR
#define SYNUI_POWER_SUPPLY_DIR "/sys/class/power_supply"
#endif

/* Is a charger plugged in?
 *
 * Straight out of /sys/class/power_supply, read at the moment it is asked
 * rather than cached off a udev watch: this is consulted when a lid shuts and
 * when the panel repaints, which is nowhere near often enough to be worth
 * keeping a subscription alive — and a cache is one more thing that can be
 * wrong at exactly the moment it matters.
 *
 * `USB` counts as well as `Mains`. USB-C charging is the normal case on a
 * modern laptop, and a machine charging over USB-PD that we called "on
 * battery" would suspend itself on a full battery. Any one supply being
 * online is enough, so an extra port sitting at online=0 cannot outvote the
 * charger that is actually connected.
 *
 * A machine with no mains-side supply at all is a desktop (or a kernel that
 * does not report one) and counts as plugged in — treating it as "on battery"
 * would apply the wrong lid setting to the case with no battery in it. */
bool power_on_ac(void)
{
    DIR *d = opendir(SYNUI_POWER_SUPPLY_DIR);
    if (!d) return true;

    bool mains_seen = false, mains_online = false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char path[288], type[32] = "";
        snprintf(path, sizeof(path), "%s/%s/type",
                 SYNUI_POWER_SUPPLY_DIR, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(type, sizeof(type), f)) type[0] = '\0';
        fclose(f);
        type[strcspn(type, "\r\n")] = '\0';

        if (strcmp(type, "Mains") != 0 && strcmp(type, "USB") != 0) continue;
        mains_seen = true;

        snprintf(path, sizeof(path), "%s/%s/online",
                 SYNUI_POWER_SUPPLY_DIR, e->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        int online = 0;
        if (fscanf(f, "%d", &online) == 1 && online) mains_online = true;
        fclose(f);
    }
    closedir(d);

    return mains_seen ? mains_online : true;
}

/* Commit every output to the state the two blank flags say it should be in.
 *
 * Recomputed from the flags rather than toggled, because two independent
 * things turn outputs off: the idle blank stage (all of them) and a lid
 * action (the built-in panel only). Toggling meant opening the lid re-enabled
 * a panel the idle stage was still legitimately holding down, and left the
 * `blanked` flag claiming otherwise — so the next input event saw nothing to
 * undo and the screen stayed black. */
static void power_apply_blank(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        bool off = s->power.blanked ||
                   (s->power.lid_blanked && output_is_internal(o->wlr_output));
        if (off == !o->wlr_output->enabled) continue;   /* already there */

        /* Mirrors the wlr-output-power-management handler in output_mgmt.c:
         * the only thing DPMS off is, at this level, is committing the output
         * disabled. */
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, !off);
        if (!wlr_output_commit_state(o->wlr_output, &state))
            wlr_log(WLR_ERROR, "synui: power: DPMS %s failed for %s",
                    off ? "off" : "on", o->wlr_output->name);
        wlr_output_state_finish(&state);

        /* Coming back up, the output has no idea what it used to show. */
        if (!off) {
            if (o->scene_output)
                wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
            wlr_output_schedule_frame(o->wlr_output);
        }
    }
}

static void power_set_blank(syn_server_t *s, bool off)
{
    if (off == (bool)s->power.blanked) return;
    s->power.blanked = off;
    power_apply_blank(s);
    wlr_log(WLR_INFO, "synui: power: displays %s", off ? "off" : "on");
}

/* The lid's half of the same thing: the built-in panel only, so a docked
 * machine keeps its monitors while the closed lid stops lighting a screen
 * nobody can see. */
static void power_set_lid_blank(syn_server_t *s, bool off)
{
    if (off == (bool)s->power.lid_blanked) return;
    s->power.lid_blanked = off;
    power_apply_blank(s);
    wlr_log(WLR_INFO, "synui: power: built-in panel %s (lid)", off ? "off" : "on");
}

/* ── Stage timers ────────────────────────────────────────── */

static int power_dim_cb(void *data)
{
    syn_server_t *s = data;
    power_set_dim(s, true);
    return 0;
}

static int power_blank_cb(void *data)
{
    syn_server_t *s = data;
    power_set_blank(s, true);
    return 0;
}

static int power_lock_cb(void *data)
{
    syn_server_t *s = data;
    if (s->power.locked) return 0;
    s->power.locked = 1;
    wlr_log(WLR_INFO, "synui: power: locking session");
    synui_lock(s);
    return 0;
}

static int power_suspend_cb(void *data)
{
    syn_server_t *s = data;

    /* Lock before going down, so the session does not come back unlocked —
     * this is what swayidle's `before-sleep` used to do for us. Only covers
     * a suspend we initiate; one triggered from outside synui still won't
     * lock. Guarded on power_lock so "lock: never" stays honoured. */
    if (s->config.power_lock > 0 && !s->power.locked) {
        s->power.locked = 1;
        synui_lock(s);
    }

    wlr_log(WLR_INFO, "synui: power: suspending");
    synui_spawn(s->config.power_suspend_cmd);
    return 0;
}

static void power_disarm(syn_server_t *s)
{
    if (s->power.t_dim)     wl_event_source_timer_update(s->power.t_dim, 0);
    if (s->power.t_blank)   wl_event_source_timer_update(s->power.t_blank, 0);
    if (s->power.t_lock)    wl_event_source_timer_update(s->power.t_lock, 0);
    if (s->power.t_suspend) wl_event_source_timer_update(s->power.t_suspend, 0);
}

/* Arm every enabled stage from now. Timeouts are seconds in the config and
 * milliseconds in the event loop; a stage with timeout 0 stays disarmed. */
static void power_arm(syn_server_t *s)
{
    power_disarm(s);

    /* An inhibitor (audio playing), the master switch being off, or a game
     * running means no stage should ever fire — leave everything disarmed.
     * Game mode is its own flag rather than a forged idle_inhibitors entry:
     * that counter belongs to the wlr idle-inhibit protocol and is mirrored to
     * the idle notifier. A gamepad produces no seat input, so without this a
     * controller-only session looks idle and the screen dims mid-game. */
    if (!s->config.power_enabled || idle_inhibited(s)) return;
    if (s->game.active && s->config.game_inhibit_idle) return;

    struct { struct wl_event_source *src; int secs; } stage[] = {
        { s->power.t_dim,     s->config.power_dim     },
        { s->power.t_blank,   s->config.power_blank   },
        { s->power.t_lock,    s->config.power_lock    },
        { s->power.t_suspend, s->config.power_suspend },
    };
    for (size_t i = 0; i < sizeof(stage) / sizeof(stage[0]); i++)
        if (stage[i].src && stage[i].secs > 0)
            wl_event_source_timer_update(stage[i].src, stage[i].secs * 1000);
}

static uint32_t power_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void power_notify_activity(syn_server_t *s)
{
    /* Whether any stage has actually fired. If one has, its one-shot timer is
     * spent and we must rearm now, throttle or no throttle. */
    bool fired = s->power.dimmed || s->power.blanked || s->power.locked;

    /* Undo the reversible stages. Lock is not reversible from here (only the
     * user can dismiss swaylock), but clearing the flag lets a later idle
     * period lock again once they have unlocked. */
    power_set_dim(s, false);
    power_set_blank(s, false);
    s->power.locked = 0;

    /* This runs on every input event, and power_arm() is four
     * timerfd_settime() calls — a 1 kHz mouse would make four thousand
     * syscalls a second just to say "still here". Timeouts have
     * second granularity, so a quarter second of slack costs nothing. */
    uint32_t now = power_now_ms();
    if (!fired && now - s->power.last_arm_ms < 250) return;
    s->power.last_arm_ms = now;

    power_arm(s);
}

/* A resume has to light the screens itself.
 *
 * Nothing else does it. The blank stage commits every output disabled, and the
 * only thing that ever re-enabled them was power_notify_activity() off an input
 * event — so after a wake the machine sat there running with all outputs still
 * committed off, and the user had to type blind to find out it was up. Not even
 * the lock screen was visible, which is what made "did it resume?" unanswerable
 * without touching the mouse.
 *
 * Deliberately NOT power_notify_activity(): that is the input path and it also
 * clears power.locked, whose contract is "power management has a lock up". A
 * resume is not input. The session was locked on the way down and still is, so
 * clearing the flag here would let power_lock_cb() fire synui_lock() a second
 * time on an already-locked session. Undo only the two reversible display
 * stages and re-arm the idle countdown from now. */
void power_wake_display(syn_server_t *s)
{
    power_set_dim(s, false);
    power_set_blank(s, false);
    power_arm(s);
}

void power_reload(syn_server_t *s)
{
    power_arm(s);
    /* The lid inhibitor is part of the config too: switching a lid row to or
     * from "system" has to hand the lid back to logind, or take it away. */
    logind_lid_update(s);
    if (s->power.visible) synui_render_power(s);
}

/* ── Laptop lid ──────────────────────────────────────────── */

/* Which of the three lid settings applies right now, and the name of the case
 * it picked (for the log and the panel's header note).
 *
 * Docked beats mains beats battery — the same order logind resolves
 * HandleLidSwitchDocked / ExternalPower / HandleLidSwitch in. Decided at the
 * moment the lid shuts rather than cached, so plugging in a monitor or a
 * charger changes the answer with nothing having to notice the event. */
static int lid_action_now(syn_server_t *s, const char **why)
{
    int a;
    if (power_docked(s))   { a = s->config.lid_close_docked_action;
                             if (why) *why = "docked"; }
    else if (power_on_ac()) { a = s->config.lid_close_ac_action;
                              if (why) *why = "plugged in"; }
    else                    { a = s->config.lid_close_action;
                              if (why) *why = "on battery"; }

    if (a < 0 || a >= SYN_LID_ACTION_COUNT) return SYN_LID_SYSTEM;
    return a;
}

/* The name of the case that is live right now — what the panel puts in its
 * header so the three rows do not have to encode their own precedence. */
const char *power_lid_case(syn_server_t *s)
{
    const char *why = "";
    lid_action_now(s, &why);
    return why;
}

/* A lid row left on `system`, honoured properly.
 *
 * If synui holds no inhibitor, logind has already acted and there is nothing
 * to do — the normal case, and the whole meaning of `system`.
 *
 * If synui *does* hold it (because one of the other two rows wanted an action
 * of its own — the inhibitor is not per-case), then logind has been stopped
 * and doing nothing here would silently turn `system` into `ignore`. On a
 * laptop going into a bag that is the difference between sleeping and cooking,
 * so ask logind what it would have done and do that.
 *
 * The handler names are systemd's, and the ones that are not `ignore` or
 * `lock` are all systemctl verbs. They are matched against a fixed list rather
 * than interpolated into the command: the value crosses a process boundary and
 * ends up in a shell, and a whitelist costs one array. */
static void power_lid_run_system(syn_server_t *s)
{
    if (!logind_holds_lid()) {
        wlr_log(WLR_INFO, "synui: power: lid left to logind");
        return;
    }

    char handler[64] = "";
    if (!logind_lid_handler(power_docked(s), power_on_ac(),
                            handler, sizeof(handler))) {
        /* Cannot ask. systemd's documented default, and what SynapseOS's own
         * drop-in says, is to suspend — the safe way to be wrong here. */
        snprintf(handler, sizeof(handler), "suspend");
        wlr_log(WLR_ERROR, "synui: power: lid 'system' but logind would not"
                " say what it does — suspending");
    }

    wlr_log(WLR_INFO, "synui: power: lid 'system' -> logind's %s", handler);

    if (strcmp(handler, "ignore") == 0)
        return;

    if (strcmp(handler, "lock") == 0) {
        if (!s->power.locked) { s->power.locked = 1; synui_lock(s); }
        power_set_lid_blank(s, true);
        return;
    }

    if (strcmp(handler, "suspend") == 0) {
        /* The user's own suspend command, not a hardcoded systemctl: they may
         * have pointed power_suspend_cmd somewhere deliberately. */
        synui_spawn(s->config.power_suspend_cmd);
        return;
    }

    static const char *const verbs[] = {
        "hibernate", "hybrid-sleep", "suspend-then-hibernate",
        "poweroff", "halt", "kexec", "reboot",
    };
    for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        if (strcmp(handler, verbs[i]) != 0) continue;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "systemctl %s", verbs[i]);
        synui_spawn(cmd);
        return;
    }

    wlr_log(WLR_ERROR, "synui: power: logind lid handler '%s' is not one synui"
            " knows — doing nothing", handler);
}

void power_lid_set(syn_server_t *s, bool closed)
{
    s->power.lid_seen = 1;
    if (closed == (bool)s->power.lid_closed) return;
    s->power.lid_closed = closed;

    if (!closed) {
        wlr_log(WLR_INFO, "synui: power: lid opened");
        /* Light the built-in panel back up whatever the action was — including
         * after a suspend, where the machine comes back with the lid already
         * open and this is the event that says so. Only the lid's own blank is
         * undone; an idle blank stays down until there is real input. */
        power_set_lid_blank(s, false);
        /* Opening the lid is the user arriving, so the idle countdown starts
         * again from here rather than from whenever they last typed. It is
         * deliberately not power_notify_activity(): that also clears
         * power.locked, and a lid that locked the session is still locked. */
        power_arm(s);
        if (s->power.visible) synui_render_power(s);
        return;
    }

    const char *why = "";
    int action = lid_action_now(s, &why);
    wlr_log(WLR_INFO, "synui: power: lid closed, %s -> %s",
            why, syn_lid_action_names[action]);

    switch (action) {
    case SYN_LID_SYSTEM:
        power_lid_run_system(s);
        break;
    case SYN_LID_IGNORE:
        break;
    case SYN_LID_LOCK:
        if (!s->power.locked) {
            s->power.locked = 1;
            synui_lock(s);
        }
        /* And blank, as SYN_LID_BLANK does: a locked session with the panel
         * still lit is a lock screen glowing inside a shut laptop. */
        power_set_lid_blank(s, true);
        break;
    case SYN_LID_BLANK:
        power_set_lid_blank(s, true);
        break;
    case SYN_LID_SUSPEND:
        /* No lock here: logind.c locks on PrepareForSleep, which covers this
         * suspend and every other one. Doing it twice would spawn a second
         * locker over the first. */
        wlr_log(WLR_INFO, "synui: power: suspending (lid)");
        synui_spawn(s->config.power_suspend_cmd);
        break;
    }

    if (s->power.visible) synui_render_power(s);
}

void power_init(syn_server_t *s)
{
    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->power.t_dim     = wl_event_loop_add_timer(loop, power_dim_cb,     s);
    s->power.t_blank   = wl_event_loop_add_timer(loop, power_blank_cb,   s);
    s->power.t_lock    = wl_event_loop_add_timer(loop, power_lock_cb,    s);
    s->power.t_suspend = wl_event_loop_add_timer(loop, power_suspend_cb, s);
    power_arm(s);
}

void power_finish(syn_server_t *s)
{
    if (s->power.t_dim)     wl_event_source_remove(s->power.t_dim);
    if (s->power.t_blank)   wl_event_source_remove(s->power.t_blank);
    if (s->power.t_lock)    wl_event_source_remove(s->power.t_lock);
    if (s->power.t_suspend) wl_event_source_remove(s->power.t_suspend);
    s->power.t_dim = s->power.t_blank = NULL;
    s->power.t_lock = s->power.t_suspend = NULL;
}

/* ── Persisted state ─────────────────────────────────────── */

static bool power_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "power.state");
}

void power_state_save(syn_server_t *s)
{
    char path[256];
    if (!power_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: power: cannot write '%s': %s",
                path, strerror(errno));
        snprintf(s->power.status, sizeof(s->power.status),
                 "save failed: %s", strerror(errno));
        return;
    }
    fprintf(f, "enabled=%d\n", s->config.power_enabled ? 1 : 0);
    fprintf(f, "dim=%d\n",     s->config.power_dim);
    fprintf(f, "blank=%d\n",   s->config.power_blank);
    fprintf(f, "lock=%d\n",    s->config.power_lock);
    fprintf(f, "suspend=%d\n", s->config.power_suspend);
    fprintf(f, "lid=%s\n",
            syn_lid_action_names[s->config.lid_close_action]);
    fprintf(f, "lid_ac=%s\n",
            syn_lid_action_names[s->config.lid_close_ac_action]);
    fprintf(f, "lid_docked=%s\n",
            syn_lid_action_names[s->config.lid_close_docked_action]);
    fclose(f);

    s->power.dirty = 0;
    snprintf(s->power.status, sizeof(s->power.status), "saved to power.state");
}

/* Only the timeouts round-trip; power_lock_cmd / power_suspend_cmd stay
 * synuirc-only, since the panel has no text entry to edit them with. */
void power_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!power_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc stands */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *sval = eq + 1;
        int val = atoi(sval);

        if      (strcmp(key, "enabled") == 0) cfg->power_enabled = val ? 1 : 0;
        else if (strcmp(key, "dim") == 0)     cfg->power_dim     = val < 0 ? 0 : val;
        else if (strcmp(key, "blank") == 0)   cfg->power_blank   = val < 0 ? 0 : val;
        else if (strcmp(key, "lock") == 0)    cfg->power_lock    = val < 0 ? 0 : val;
        else if (strcmp(key, "suspend") == 0) cfg->power_suspend = val < 0 ? 0 : val;
        /* The lid keys are names, not numbers — a bad one leaves the synuirc
         * value standing rather than silently selecting action 0 ("system"),
         * which atoi() would have done to every one of them. */
        else if (strcmp(key, "lid") == 0) {
            int a = lid_action_from_name(sval);
            if (a >= 0) cfg->lid_close_action = a;
        } else if (strcmp(key, "lid_ac") == 0) {
            int a = lid_action_from_name(sval);
            if (a >= 0) cfg->lid_close_ac_action = a;
        } else if (strcmp(key, "lid_docked") == 0) {
            int a = lid_action_from_name(sval);
            if (a >= 0) cfg->lid_close_docked_action = a;
        }
    }
    fclose(f);
}

/* ── Panel ───────────────────────────────────────────────── */

void power_show(syn_server_t *s)
{
    s->power.visible  = 1;
    s->power.selected = POWER_ROW_ENABLED;
    s->power.status[0] = '\0';
    synui_render_power(s);
}

void power_hide(syn_server_t *s)
{
    s->power.visible = 0;
    synui_render_power(s);
}

void power_toggle(syn_server_t *s)
{
    if (s->power.visible) power_hide(s);
    else                  power_show(s);
}

/* Step the selected row's timeout along the ladder. Lands on the next rung
 * strictly past the current value, so a timeout loaded from synuirc that
 * sits between rungs (say 500) still moves on the first keypress. */
static void power_adjust(syn_server_t *s, int dir)
{
    /* Lid rows step a list of actions, not the timeout ladder. Wraps, because
     * the list is five short and stepping off one end to reach the other beats
     * hitting a wall on a row where every value is equally valid. */
    int *action = row_action_field(s, s->power.selected);
    if (action) {
        int next = (*action + dir) % SYN_LID_ACTION_COUNT;
        if (next < 0) next += SYN_LID_ACTION_COUNT;
        *action = next;
        s->power.dirty = 1;
        snprintf(s->power.status, sizeof(s->power.status), "%s: %s",
                 row_label(s->power.selected), syn_lid_action_names[next]);
        /* Whether synui or logind owns the lid just changed. */
        logind_lid_update(s);
        return;
    }

    int *field = row_field(s, s->power.selected);
    if (!field) {                       /* the master switch row */
        s->config.power_enabled = !s->config.power_enabled;
        s->power.dirty = 1;
        snprintf(s->power.status, sizeof(s->power.status),
                 "power saving %s", s->config.power_enabled ? "enabled" : "disabled");
        power_arm(s);
        return;
    }

    int cur = *field, next = cur;
    if (dir > 0) {
        for (int i = 0; i < power_ladder_len; i++)
            if (power_ladder[i] > cur) { next = power_ladder[i]; break; }
    } else {
        for (int i = power_ladder_len - 1; i >= 0; i--)
            if (power_ladder[i] < cur) { next = power_ladder[i]; break; }
    }
    if (next == cur) return;            /* already at an end of the ladder */
    *field = next;
    s->power.dirty = 1;

    char t[32];
    power_format_timeout(next, t, sizeof(t));
    snprintf(s->power.status, sizeof(s->power.status), "%s: %s",
             row_label(s->power.selected), t);

    /* Retuning a timeout restarts the idle period, so the new value is
     * measured from now rather than from whenever the user last typed. */
    power_arm(s);
}

int power_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->power.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        power_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->power.selected > 0) s->power.selected--;
        synui_render_power(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->power.selected < POWER_ROW_COUNT - 1) s->power.selected++;
        synui_render_power(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        power_adjust(s, -1);
        synui_render_power(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        power_adjust(s, +1);
        synui_render_power(s);
        return 1;
    case XKB_KEY_space:
        /* Toggle the selected stage off, or back to its ladder default. */
        {
            int *field = row_field(s, s->power.selected);
            /* The master switch and the lid rows have no timeout to zero, so
             * Space just steps them the way Right does. */
            if (!field) { power_adjust(s, +1); synui_render_power(s); return 1; }
            static const int fallback[] = {
                [POWER_ROW_DIM] = 240, [POWER_ROW_BLANK]   = 600,
                [POWER_ROW_LOCK] = 900, [POWER_ROW_SUSPEND] = 0,
            };
            *field = *field ? 0 : fallback[s->power.selected];
            s->power.dirty = 1;
            power_arm(s);
            synui_render_power(s);
            return 1;
        }
    case XKB_KEY_s:
        power_state_save(s);
        synui_render_power(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}

/* Rendering lives here rather than render.c only because every other panel's
 * draw does; keep it beside them. */
int power_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                     char *value, size_t vn)
{
    snprintf(name, nn, "%s", row_label(row));

    if (row == POWER_ROW_ENABLED) {
        snprintf(value, vn, "%s",
                 s->config.power_enabled ? "enabled" : "disabled");
        return !s->config.power_enabled;
    }

    int *action = row_action_field(s, row);
    if (action) {
        snprintf(value, vn, "%s", syn_lid_action_names[*action]);
        /* "system" is inert *for synui* — logind is still doing something with
         * the lid, so it greys out the same way "ignore" does without either
         * one claiming the lid is dead. */
        return *action == SYN_LID_IGNORE || *action == SYN_LID_SYSTEM;
    }

    int secs = *row_field(s, row);
    power_format_timeout(secs, value, vn);
    return secs <= 0;
}
