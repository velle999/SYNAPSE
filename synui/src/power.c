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
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* How dark the dim stage goes. Dark enough to read as "about to blank",
 * light enough that you can still see what you were doing. */
#define POWER_DIM_ALPHA 0.65f

/* Timeouts the panel's Left/Right steps through. 0 first, so stepping left
 * off the bottom lands on "never" rather than wrapping to two hours. */
static const int power_ladder[] = {
    0, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1800, 2700, 3600, 7200,
};
static const int power_ladder_len =
    (int)(sizeof(power_ladder) / sizeof(power_ladder[0]));

/* ── Config field <-> panel row ──────────────────────────── */

/* The timeout a row edits. POWER_ROW_ENABLED has no timeout — it toggles the
 * master switch — so it maps to NULL and callers must check. */
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

static const char *row_label(int row)
{
    switch (row) {
    case POWER_ROW_ENABLED: return "Power saving";
    case POWER_ROW_DIM:     return "Dim screen";
    case POWER_ROW_BLANK:   return "Blank displays";
    case POWER_ROW_LOCK:    return "Lock session";
    case POWER_ROW_SUSPEND: return "Suspend system";
    default:                return "?";
    }
}

/* "never" / "45s" / "5m" / "1m30s" — compact enough for a table column. */
static void power_format_timeout(int secs, char *buf, size_t n)
{
    if (secs <= 0)           snprintf(buf, n, "never");
    else if (secs < 60)      snprintf(buf, n, "%ds", secs);
    else if (secs % 60 == 0) snprintf(buf, n, "%dm", secs / 60);
    else                     snprintf(buf, n, "%dm%ds", secs / 60, secs % 60);
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

/* Mirrors the wlr-output-power-management handler in output_mgmt.c: the only
 * thing DPMS off is, at this level, is committing the output disabled. */
static void power_set_blank(syn_server_t *s, bool off)
{
    if (off == (bool)s->power.blanked) return;
    s->power.blanked = off;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
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

    wlr_log(WLR_INFO, "synui: power: displays %s", off ? "off" : "on");
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
    synui_spawn(s->config.power_lock_cmd);
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
        synui_spawn(s->config.power_lock_cmd);
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
    if (!s->config.power_enabled || s->idle_inhibitors > 0) return;
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

void power_reload(syn_server_t *s)
{
    power_arm(s);
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
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    snprintf(buf, n, "%s/.config/synui/power.state", home);
    return true;
}

void power_state_save(syn_server_t *s)
{
    char path[256];
    if (!power_state_path(path, sizeof(path))) return;

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
        const char *key = line;
        int val = atoi(eq + 1);

        if      (strcmp(key, "enabled") == 0) cfg->power_enabled = val ? 1 : 0;
        else if (strcmp(key, "dim") == 0)     cfg->power_dim     = val < 0 ? 0 : val;
        else if (strcmp(key, "blank") == 0)   cfg->power_blank   = val < 0 ? 0 : val;
        else if (strcmp(key, "lock") == 0)    cfg->power_lock    = val < 0 ? 0 : val;
        else if (strcmp(key, "suspend") == 0) cfg->power_suspend = val < 0 ? 0 : val;
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
            if (!field) { power_adjust(s, +1); synui_render_power(s); return 1; }
            static const int fallback[POWER_ROW_COUNT] = { 0, 240, 600, 900, 0 };
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
void power_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                      char *value, size_t vn)
{
    snprintf(name, nn, "%s", row_label(row));
    if (row == POWER_ROW_ENABLED)
        snprintf(value, vn, "%s", s->config.power_enabled ? "enabled" : "disabled");
    else
        power_format_timeout(*row_field(s, row), value, vn);
}
