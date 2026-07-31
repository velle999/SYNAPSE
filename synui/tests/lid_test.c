/*
 * lid_test.c — laptop lid actions, docked detection, and lid persistence.
 *
 * There is no way to close a lid headless: wlroots' headless backend has no
 * input devices at all, so it has no switches either, and the only other way to
 * produce a real WLR_SWITCH_TYPE_LID event is a uinput device — which would be
 * picked up by the *live* session's compositor and genuinely suspend the
 * machine. So the lid is exercised where its decisions actually live: power.c's
 * model, linked against stubs for the compositor it normally talks to, driving
 * power_lid_set() directly — the same call input.c makes from the switch
 * device's toggle handler.
 *
 * What that buys, none of which the build catches:
 *   - which of the three settings a closed lid uses, and that they resolve
 *     docked-then-mains-then-battery the way logind's own three do
 *   - that `blank` turns the built-in panel off and leaves monitors alone,
 *     which is the entire reason the docked setting exists
 *   - that opening the lid does not undo an idle blank that is still running
 *   - that a `system` row does what logind would have done rather than nothing
 *     when synui is holding the lid inhibitor for one of the other rows
 *   - that power.state round-trips action *names*, and that a name it does not
 *     recognise leaves the configured value standing
 *
 * Run as:
 *     ninja -C build && ./build/lid_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/types/wlr_output.h>

#include "synui.h"

/* ── The compositor, stubbed ─────────────────────────────── */

static int spawned_suspend, locked, rendered;
static char last_spawn[256];

void synui_spawn(const char *cmd)
{
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
    spawned_suspend++;
}
void synui_lock(syn_server_t *s)          { (void)s; locked++; }
void synui_render_power(syn_server_t *s)  { (void)s; rendered++; }
void logind_lid_update(syn_server_t *s)   { (void)s; }

/* Whether synui holds logind's lid inhibitor, and what logind would have done
 * — both driven by the test, because a `system` row means different things
 * depending on the first and has to do the second when it is true. */
static bool holds_lid;
static const char *logind_handler = "suspend";

bool logind_holds_lid(void) { return holds_lid; }

bool logind_lid_handler(bool docked, bool on_ac, char *buf, size_t n)
{
    (void)docked; (void)on_ac;
    if (!logind_handler) return false;      /* the "cannot ask logind" path */
    snprintf(buf, n, "%s", logind_handler);
    return true;
}
/* idle_inhibited() is a static inline in synui.h and needs no stub — the two
 * counters it reads are zero in a calloc'd server, which is "nothing is
 * holding the screen on". */

/* power.state goes in a scratch dir, never the caller's real config. */
static char scratch[128];

bool syn_config_path(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", scratch, name);
    return true;
}
void syn_config_ensure_dir(void) { }

/* wlroots symbols overridden by strong definitions here, the same trick
 * deskicon_drag_test uses: committing state on a fake wlr_output would walk an
 * impl that does not exist. The commit is what the DPMS stage *is*, so it is
 * recorded rather than skipped — the test asserts on which outputs got one. */
static int commits;

bool wlr_output_commit_state(struct wlr_output *output,
                             const struct wlr_output_state *state)
{
    commits++;
    output->enabled = state->enabled;
    return true;
}
void wlr_output_schedule_frame(struct wlr_output *output) { (void)output; }

/* ── Fixture ─────────────────────────────────────────────── */

/* A laptop panel and an external monitor, either of which can be absent. The
 * connector names are the whole input to output_is_internal(), which is what
 * decides "docked". */
static syn_server_t *srv;
static syn_output_t *panel, *monitor;
static struct wlr_output panel_out, monitor_out;

static syn_output_t *add_output(char *name, struct wlr_output *wlr)
{
    syn_output_t *o = calloc(1, sizeof(*o));
    assert(o);
    wlr->name = name;
    wlr->enabled = true;
    o->wlr_output = wlr;
    o->scene_output = NULL;      /* skips the damage-ring path */
    wl_list_insert(srv->outputs.prev, &o->link);
    return o;
}

/* The fake /sys/class/power_supply power_on_ac() reads (meson points
 * SYNUI_POWER_SUPPLY_DIR here). Rewritten per case: a machine running this
 * test is almost certainly plugged in, so the on-battery branch is otherwise
 * unreachable. BAT0 is always present — a laptop with no battery at all is not
 * the case being modelled, and its presence is what proves the type filter
 * works rather than the loop simply matching the first entry. */
static void set_ac(bool online)
{
    char p[512];
    snprintf(p, sizeof(p), "%s", SYNUI_POWER_SUPPLY_DIR);
    mkdir(p, 0700);

    struct { const char *name, *type, *online; } sup[] = {
        { "AC",   "Mains",   online ? "1" : "0" },
        { "BAT0", "Battery", NULL },
    };
    for (size_t i = 0; i < sizeof(sup) / sizeof(sup[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", SYNUI_POWER_SUPPLY_DIR, sup[i].name);
        mkdir(p, 0700);

        char f[600];
        snprintf(f, sizeof(f), "%s/type", p);
        FILE *fp = fopen(f, "w");
        assert(fp);
        fprintf(fp, "%s\n", sup[i].type);
        fclose(fp);

        if (!sup[i].online) continue;
        snprintf(f, sizeof(f), "%s/online", p);
        fp = fopen(f, "w");
        assert(fp);
        fprintf(fp, "%s\n", sup[i].online);
        fclose(fp);
    }
}

/* Back to a known state between cases: lid open, nothing blanked, both
 * outputs on, counters zeroed. */
static void reset(bool docked, bool on_ac)
{
    wl_list_init(&srv->outputs);
    panel = add_output("eDP-1", &panel_out);
    monitor = docked ? add_output("DP-2", &monitor_out) : NULL;
    set_ac(on_ac);

    memset(&srv->power, 0, sizeof(srv->power));
    srv->config.power_enabled = 1;
    /* Neither of these applies unless a case reaches SYN_LID_SYSTEM. */
    holds_lid = false;
    logind_handler = "suspend";
    spawned_suspend = locked = rendered = commits = 0;
    last_spawn[0] = '\0';
}

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n");  \
                   failures++; }                                \
} while (0)

static int failures;

int main(void)
{
    snprintf(scratch, sizeof(scratch), "/tmp/synui-lid-test-%d", (int)getpid());
    if (mkdir(scratch, 0700) != 0) { perror("mkdir"); return 1; }

    srv = calloc(1, sizeof(*srv));
    assert(srv);
    snprintf(srv->config.power_suspend_cmd, sizeof(srv->config.power_suspend_cmd),
             "systemctl suspend");

    /* 1. Undocked, on battery, suspend: the systemd default, done by synui. */
    reset(false, false);
    srv->config.lid_close_action        = SYN_LID_SUSPEND;
    srv->config.lid_close_docked_action = SYN_LID_IGNORE;
    CHECK(!power_docked(srv), "a lone eDP-1 must not read as docked");
    CHECK(!power_on_ac(), "an offline Mains supply must read as on battery");
    CHECK(strcmp(power_lid_case(srv), "on battery") == 0,
          "live case is '%s', not 'on battery'", power_lid_case(srv));
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 1, "undocked lid close did not suspend");
    CHECK(strcmp(last_spawn, "systemctl suspend") == 0,
          "suspended with '%s', not power_suspend_cmd", last_spawn);

    /* 2. Docked takes the *other* setting — with the same config as case 1,
     *    which is the point: one connector changes the answer. */
    reset(true, false);
    srv->config.lid_close_action        = SYN_LID_SUSPEND;
    srv->config.lid_close_docked_action = SYN_LID_IGNORE;
    CHECK(power_docked(srv), "eDP-1 + DP-2 must read as docked");
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0, "docked lid close suspended anyway");
    CHECK(commits == 0, "docked+ignore turned an output off");
    CHECK(panel_out.enabled && monitor_out.enabled, "ignore blanked something");

    /* 3. Docked + blank: the built-in panel goes, the monitor stays. This is
     *    the case the whole docked setting exists for. */
    reset(true, false);
    srv->config.lid_close_docked_action = SYN_LID_BLANK;
    power_lid_set(srv, true);
    CHECK(!panel_out.enabled, "lid blank left the built-in panel lit");
    CHECK(monitor_out.enabled, "lid blank turned the external monitor off too");
    CHECK(spawned_suspend == 0 && locked == 0, "blank did more than blank");

    /*    ...and opening it puts the panel back. */
    power_lid_set(srv, false);
    CHECK(panel_out.enabled, "opening the lid left the panel dark");
    CHECK(monitor_out.enabled, "opening the lid disturbed the monitor");

    /* 4. Docked + lock: locks *and* blanks. A lock screen glowing inside a
     *    shut laptop is the bug this half asserts against. */
    reset(true, false);
    srv->config.lid_close_docked_action = SYN_LID_LOCK;
    power_lid_set(srv, true);
    CHECK(locked == 1, "lid lock did not lock");
    CHECK(!panel_out.enabled, "lid lock left the built-in panel lit");
    CHECK(monitor_out.enabled, "lid lock turned the external monitor off");

    /* 5. AC beats battery: same config as case 1, charger plugged in, and the
     *    lid must take the *other* row. This is the axis logind splits out as
     *    HandleLidSwitchExternalPower. */
    reset(false, true);
    srv->config.lid_close_action    = SYN_LID_SUSPEND;
    srv->config.lid_close_ac_action = SYN_LID_BLANK;
    CHECK(power_on_ac(), "an online Mains supply must read as plugged in");
    CHECK(strcmp(power_lid_case(srv), "plugged in") == 0,
          "live case is '%s', not 'plugged in'", power_lid_case(srv));
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0, "plugged in took the battery row and suspended");
    CHECK(!panel_out.enabled, "plugged in + blank left the panel lit");

    /* 6. Docked beats AC. All three rows differ, a monitor and a charger are
     *    both attached, and docked has to win — logind's own precedence. */
    reset(true, true);
    srv->config.lid_close_action        = SYN_LID_SUSPEND;
    srv->config.lid_close_ac_action     = SYN_LID_LOCK;
    srv->config.lid_close_docked_action = SYN_LID_IGNORE;
    CHECK(strcmp(power_lid_case(srv), "docked") == 0,
          "live case is '%s', not 'docked'", power_lid_case(srv));
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0 && locked == 0 && commits == 0,
          "docked did not beat plugged-in");

    /* 7. system with no inhibitor: logind has already acted by the time we
     *    hear about it, so synui must do nothing at all. */
    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = false;
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0 && locked == 0 && commits == 0,
          "'system' acted on the lid instead of leaving it to logind");

    /* 8. system *while synui holds the inhibitor* — the mixed-config trap.
     *    One row wanting an action of its own makes synui hold the lid for
     *    every case, so a `system` row that did nothing would quietly become
     *    `ignore`: a laptop that never sleeps in a bag. It has to do what
     *    logind would have done instead. */
    reset(false, false);
    srv->config.lid_close_action        = SYN_LID_SYSTEM;
    srv->config.lid_close_docked_action = SYN_LID_BLANK;   /* why we hold it */
    holds_lid = true;
    logind_handler = "suspend";
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 1, "'system' did nothing while synui held the lid");
    CHECK(strcmp(last_spawn, "systemctl suspend") == 0,
          "'system' suspend used '%s', not power_suspend_cmd", last_spawn);

    /*    ...and it follows logind rather than assuming suspend. */
    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = true;
    logind_handler = "ignore";
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0 && locked == 0 && commits == 0,
          "logind says ignore, synui did something anyway");

    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = true;
    logind_handler = "hibernate";
    power_lid_set(srv, true);
    CHECK(strcmp(last_spawn, "systemctl hibernate") == 0,
          "logind says hibernate, synui ran '%s'", last_spawn);

    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = true;
    logind_handler = "lock";
    power_lid_set(srv, true);
    CHECK(locked == 1 && !panel_out.enabled,
          "logind says lock, synui did not lock and blank");

    /*    An unreadable property must not mean "do nothing": suspending is
     *    systemd's documented default and the safe way to be wrong. */
    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = true;
    logind_handler = NULL;
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 1, "'system' with no answer from logind did nothing");

    /*    A handler synui cannot map does nothing rather than guessing — and
     *    is never interpolated into a command. */
    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SYSTEM;
    holds_lid = true;
    logind_handler = "suspend; rm -rf /";
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 0, "an unmapped logind handler was run: '%s'",
          last_spawn);

    /* 6. A lid open must not undo an idle blank that is still running. The
     *    two blanks are independent, and the first cut of this recomputed
     *    nothing: opening the lid re-enabled every output while the idle
     *    stage still believed they were off, so the next keypress had nothing
     *    left to undo and the screen stayed black. */
    reset(true, false);
    srv->config.lid_close_docked_action = SYN_LID_BLANK;
    power_lid_set(srv, true);
    srv->power.blanked = 1;                     /* as the idle stage would */
    power_lid_set(srv, false);
    CHECK(!panel_out.enabled && !monitor_out.enabled,
          "opening the lid cancelled a running idle blank");

    /* Repeats are ignored — a switch that re-reports its state must not
     * suspend twice. */
    reset(false, false);
    srv->config.lid_close_action = SYN_LID_SUSPEND;
    power_lid_set(srv, true);
    power_lid_set(srv, true);
    CHECK(spawned_suspend == 1, "a repeated 'lid closed' suspended twice");

    /* power.state round-trips names, not indices — all three of them. */
    reset(false, false);
    srv->config.lid_close_action        = SYN_LID_LOCK;
    srv->config.lid_close_ac_action     = SYN_LID_IGNORE;
    srv->config.lid_close_docked_action = SYN_LID_BLANK;
    power_state_save(srv);

    syn_config_t loaded = { .lid_close_action = SYN_LID_SYSTEM,
                            .lid_close_ac_action = SYN_LID_SYSTEM,
                            .lid_close_docked_action = SYN_LID_SYSTEM };
    power_state_load(&loaded);
    CHECK(loaded.lid_close_action == SYN_LID_LOCK,
          "lid action did not survive power.state");
    CHECK(loaded.lid_close_ac_action == SYN_LID_IGNORE,
          "AC lid action did not survive power.state");
    CHECK(loaded.lid_close_docked_action == SYN_LID_BLANK,
          "docked lid action did not survive power.state");

    /* An action name power.state does not know leaves the configured value
     * alone. atoi() would have made every one of them "system". */
    {
        char path[256];
        syn_config_path(path, sizeof(path), "power.state");
        FILE *f = fopen(path, "w");
        assert(f);
        fprintf(f, "lid=hibernate\nlid_docked=3\n");
        fclose(f);

        syn_config_t bad = { .lid_close_action = SYN_LID_SUSPEND,
                             .lid_close_docked_action = SYN_LID_LOCK };
        power_state_load(&bad);
        CHECK(bad.lid_close_action == SYN_LID_SUSPEND,
              "an unknown lid action overwrote the configured one");
        CHECK(bad.lid_close_docked_action == SYN_LID_LOCK,
              "a numeric lid action was accepted as a name");
    }

    /* Every action name parses back to its own enum value — the table and the
     * enum drifting apart is the one way all of the above still passes while
     * the config file means something else. */
    for (int i = 0; i < SYN_LID_ACTION_COUNT; i++)
        CHECK(lid_action_from_name(syn_lid_action_names[i]) == i,
              "'%s' parses to %d, not %d", syn_lid_action_names[i],
              lid_action_from_name(syn_lid_action_names[i]), i);
    CHECK(lid_action_from_name("nonsense") == -1, "a junk action name parsed");

    {
        char path[256];
        syn_config_path(path, sizeof(path), "power.state");
        unlink(path);
        rmdir(scratch);
    }

    if (failures) { printf("lid_test: %d failure(s)\n", failures); return 1; }
    printf("lid_test: OK\n");
    return 0;
}
