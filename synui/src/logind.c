/*
 * logind.c — systemd-logind integration: lock before sleep, and brightness.
 *
 * Two laptop things synui could not do, both of which logind already offers for
 * free over sd-bus — so neither needs root, a udev rule, a helper binary, or a
 * shell-out to brightnessctl.
 *
 * LOCK BEFORE SLEEP. power.c locks before a suspend *it* initiates, and its own
 * comment admits the hole: "one triggered from outside synui still won't lock."
 * On a laptop that is every suspend that matters — closing the lid goes through
 * logind (HandleLidSwitch=suspend is systemd's default), so the machine slept
 * and came back to an unlocked desktop. Anyone who closes a laptop expects the
 * opposite.
 *
 * The fix is the mechanism swayidle's `before-sleep` and systemd-lock-handler
 * use: take a **delay** inhibitor on "sleep", and when logind announces
 * PrepareForSleep(true), lock and then release the inhibitor. logind waits for
 * the fd to close (up to InhibitDelayMaxSec, 5s by default) before it suspends,
 * so the lock is up before the screen goes. A *block* inhibitor would be wrong:
 * that refuses to sleep at all.
 *
 * BRIGHTNESS. login1's Session.SetBrightness(subsystem, name, value) writes the
 * backlight for the session's own seat, so an active local session can set it
 * with no privileges. The alternatives are all worse: writing
 * /sys/class/backlight directly needs root or a udev rule granting the video
 * group, and brightnessctl is another package doing exactly this call.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>     /* F_DUPFD_CLOEXEC — the inhibitor fds are duped */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

#define LOGIND_SVC      "org.freedesktop.login1"
#define LOGIND_MGR_PATH "/org/freedesktop/login1"
#define LOGIND_MGR_IF   "org.freedesktop.login1.Manager"
/* "auto" resolves to the caller's own session, so there is no session id to
 * discover or keep in step. */
#define LOGIND_SES_PATH "/org/freedesktop/login1/session/auto"
#define LOGIND_SES_IF   "org.freedesktop.login1.Session"

static struct {
    sd_bus                 *bus;
    struct wl_event_source *src;
    /* The delay inhibitor. Holding this fd is what makes logind wait; closing it
     * is what lets the machine sleep. -1 when we hold none. */
    int                     inhibit_fd;

    /* The handle-lid-switch *block* inhibitor. Held whenever synui has a lid
     * action of its own, so logind does not suspend the machine before power.c
     * gets a look at the switch. -1 when logind still owns the lid. */
    int                     lid_fd;

    /* Backlight, resolved once from sysfs. name[0] == 0 means this machine has
     * none (a desktop), and the brightness binds then do nothing. */
    char                    bl_name[64];
    int                     bl_max;
} lg = { .inhibit_fd = -1 };

/* ── Inhibitors ──────────────────────────────────────────── */

/* Take one logind inhibitor and return the fd that holds it, or -1. Closing
 * that fd is the only way to release it, so every caller owns what it gets. */
static int logind_inhibit(const char *what, const char *why, const char *mode)
{
    if (!lg.bus) return -1;

    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int out_fd = -1;

    int r = sd_bus_call_method(lg.bus, LOGIND_SVC, LOGIND_MGR_PATH, LOGIND_MGR_IF,
                               "Inhibit", &err, &reply, "ssss",
                               what, "synui", why, mode);
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: logind: cannot take %s inhibitor: %s",
                what, err.message ? err.message : strerror(-r));
        goto out;
    }

    int fd = -1;
    r = sd_bus_message_read(reply, "h", &fd);
    if (r < 0 || fd < 0) {
        wlr_log(WLR_ERROR, "synui: logind: %s inhibitor reply had no fd", what);
        goto out;
    }

    /* The fd belongs to the message and is closed with it — dup it, or the
     * inhibitor evaporates the moment this reply is unreffed and whatever it
     * was holding off comes back without a single error to show for it. */
    out_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (out_fd < 0)
        wlr_log(WLR_ERROR, "synui: logind: dup inhibitor fd: %s", strerror(errno));

out:
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return out_fd;
}

static void logind_take_inhibitor(void)
{
    if (!lg.bus || lg.inhibit_fd >= 0) return;

    /* "delay", not "block": block refuses sleep outright. delay just buys us
     * the few seconds needed to get the lock up first. */
    lg.inhibit_fd = logind_inhibit("sleep", "Lock the session before sleep",
                                   "delay");
}

static void logind_drop_inhibitor(void)
{
    if (lg.inhibit_fd >= 0) { close(lg.inhibit_fd); lg.inhibit_fd = -1; }
}

/* ── Lid ─────────────────────────────────────────────────── */

/*
 * synui and logind cannot both act on the lid. logind's default
 * HandleLidSwitch=suspend fires the moment the switch closes, which would beat
 * power.c's action to it every time and make "Lid closed: blank" — the setting
 * a docked laptop actually wants — do nothing but suspend the machine.
 *
 * So: any lid action other than SYN_LID_SYSTEM takes a *block* inhibitor on
 * handle-lid-switch, which is the documented way to say "this session handles
 * the lid" (it is what GNOME and KDE do, and the polkit action is allow_active
 * = yes, so it needs no privileges). SYN_LID_SYSTEM drops it and hands the lid
 * straight back to logind.conf.
 *
 * Failing to get it is not fatal and deliberately not retried: logind then
 * still suspends on lid close, which is the same behaviour the machine had
 * before any of this existed. If synui dies the fd dies with it and logind
 * takes the lid back on its own — the failure mode is "the default", never "a
 * laptop that no longer sleeps when shut".
 */
void logind_lid_update(syn_server_t *s)
{
    /* No system bus (a nested or headless synui) — logind_init already said so
     * once, and there is nothing to inhibit. Silent, or every panel keypress
     * would log an error. */
    if (!lg.bus) return;

    /* Any one case wanting synui to act means synui has to hold the inhibitor
     * for all of them: the inhibitor is not per-case, and which case is live
     * is not known until the lid actually shuts. A row left on `system` is
     * then honoured by power.c doing nothing — which is not quite what logind
     * would have done, and is why the panel greys those rows rather than
     * claiming they are active. */
    bool want = s->config.lid_close_action        != SYN_LID_SYSTEM ||
                s->config.lid_close_ac_action     != SYN_LID_SYSTEM ||
                s->config.lid_close_docked_action != SYN_LID_SYSTEM;

    if (want == (lg.lid_fd >= 0)) return;

    if (!want) {
        close(lg.lid_fd);
        lg.lid_fd = -1;
        wlr_log(WLR_INFO, "synui: logind: lid handed back to logind");
        return;
    }

    lg.lid_fd = logind_inhibit("handle-lid-switch",
                               "synui handles the lid switch", "block");

    if (lg.lid_fd >= 0)
        wlr_log(WLR_INFO, "synui: logind: handling the lid switch");
    else
        wlr_log(WLR_ERROR, "synui: logind: no handle-lid-switch inhibitor —"
                " logind keeps the lid, so the lid rows in Super+P will not"
                " take effect");
}

bool logind_holds_lid(void)
{
    return lg.lid_fd >= 0;
}

/*
 * What logind itself would have done with this lid close.
 *
 * Needed because holding the inhibitor is all-or-nothing while the *setting*
 * is per-case: with, say, docked=blank and battery=system, synui holds the lid
 * for every case, and a `system` case would otherwise mean "nothing happens"
 * rather than "logind's policy". That is the difference between a laptop that
 * sleeps when it goes in a bag and one that cooks in it, so it is not
 * something to leave as a documented quirk.
 *
 * logind publishes the three handlers as plain string properties, so this is
 * exact rather than a guess at what logind.conf might say — and it picks up an
 * edited drop-in with no synui restart. Values are the same vocabulary
 * systemd documents for HandleLidSwitch=: ignore, lock, suspend, hibernate,
 * poweroff, and friends.
 */
bool logind_lid_handler(bool docked, bool on_ac, char *buf, size_t n)
{
    if (!lg.bus) return false;

    const char *prop = docked ? "HandleLidSwitchDocked"
                     : on_ac  ? "HandleLidSwitchExternalPower"
                              : "HandleLidSwitch";

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *val = NULL;
    bool ok = false;

    int r = sd_bus_get_property(lg.bus, LOGIND_SVC, LOGIND_MGR_PATH,
                                LOGIND_MGR_IF, prop, &err, &reply, "s");
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: logind: cannot read %s: %s", prop,
                err.message ? err.message : strerror(-r));
        goto out;
    }
    if (sd_bus_message_read(reply, "s", &val) < 0 || !val) goto out;

    snprintf(buf, n, "%s", val);
    ok = true;

out:
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return ok;
}

/* PrepareForSleep(true)  — about to sleep: lock, then get out of the way.
 * PrepareForSleep(false) — just resumed: take the inhibitor again for next time.
 */
static int on_prepare_for_sleep(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    int going_to_sleep;

    if (sd_bus_message_read(m, "b", &going_to_sleep) < 0) return 0;

    if (going_to_sleep) {
        /* Honour "lock: never" — the same guard power_suspend_cb uses. Someone
         * who turned locking off did not ask for it back at the lid. */
        if (s->config.power_lock > 0 && !s->power.locked) {
            wlr_log(WLR_INFO, "synui: logind: sleep imminent — locking");
            s->power.locked = 1;
            synui_lock(s);
        }
        /*
         * This used to KILL the wallpaper engines here, bounded to ~2.3s so it
         * fit inside InhibitDelayMaxSec, because one caught exiting inside
         * nvidia_uvm's teardown aborted a suspend on 2026-07-28 and wedged DP-3
         * until reboot. It now only reports them: nvidia-utils ships the
         * no-freeze-session drop-in that the abort really needed, and killing a
         * CUDA holder on the way into suspend is what puts one in teardown in
         * the first place. Full reasoning at wpengine_note_before_sleep().
         *
         * Cheap and non-blocking, so it no longer matters that this is the last
         * moment we hold the delay.
         */
        wpengine_note_before_sleep();

        /* Release the delay so the machine can actually sleep. The lock command
         * has been spawned; logind gives us InhibitDelayMaxSec (5s) and we do
         * not need it — swaylock maps and takes the session on its own clock. */
        logind_drop_inhibitor();
    } else {
        wlr_log(WLR_INFO, "synui: logind: resumed");
        logind_take_inhibitor();

        /* Light the screens back up. Until this existed, a wake left every
         * output committed off until the user produced an input event, so the
         * machine looked dead on resume and the only way to learn it was up
         * was to type at a black screen. */
        power_wake_display(s);

        /* And show the clock. The lock pane fades down while it sits idle, so
         * un-blanking on its own can reveal a screen that is dark for a second
         * reason. This brightens it and restarts the fade hold, which puts the
         * clock in front of the user at the moment the machine is ready — the
         * "it's awake now" signal there was no way to see before. No-op when
         * the session is not locked; the bar's clock is already on screen. */
        lock_notify_activity(s);

        /* A Workshop wallpaper does not survive a suspend: linux-wallpaperengine
         * loses its layer surfaces and cannot rebuild them, so the screen wakes
         * to whatever wallpaper.c paints underneath. Armed here as well as from
         * server_new_output() because the surfaces can go without synui having
         * destroyed an output — this is the one signal that fires either way.
         * Both share a timer, so a resume that also recreates the connectors
         * still restarts the engine exactly once. */
        wpengine_restore_soon(s);
    }
    return 0;
}

/* ── Brightness ──────────────────────────────────────────── */

/* The first backlight sysfs has. Machines with a real panel have exactly one
 * that matters; a desktop has none, and the binds then do nothing rather than
 * erroring at someone who has no brightness to set. */
static void logind_find_backlight(void)
{
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char path[256];
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness",
                 e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int max = 0;
        int got = fscanf(f, "%d", &max);
        fclose(f);
        if (got != 1 || max <= 0) continue;

        snprintf(lg.bl_name, sizeof(lg.bl_name), "%s", e->d_name);
        lg.bl_max = max;
        wlr_log(WLR_INFO, "synui: logind: backlight %s (max %d)", lg.bl_name, max);
        break;
    }
    closedir(d);
}

static int logind_brightness_now(void)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", lg.bl_name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int cur = -1;
    if (fscanf(f, "%d", &cur) != 1) cur = -1;
    fclose(f);
    return cur;
}

void logind_brightness_step(syn_server_t *s, int pct)
{
    (void)s;
    if (!lg.bus || !lg.bl_name[0]) return;

    int cur = logind_brightness_now();
    if (cur < 0) return;

    /* Step by a percentage of the range, not a raw count: max_brightness is
     * anywhere from 15 to 96000 depending on the panel, so a fixed step is
     * either imperceptible or the whole scale. */
    int delta = lg.bl_max * pct / 100;
    if (delta == 0) delta = pct > 0 ? 1 : -1;

    long next = (long)cur + delta;
    if (next > lg.bl_max) next = lg.bl_max;
    /* Never all the way to 0: a black panel looks exactly like a crashed
     * machine, and the key to undo it cannot be seen. */
    if (next < 1) next = 1;
    if (next == cur) return;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(lg.bus, LOGIND_SVC, LOGIND_SES_PATH, LOGIND_SES_IF,
                               "SetBrightness", &err, NULL, "ssu",
                               "backlight", lg.bl_name, (uint32_t)next);
    if (r < 0)
        wlr_log(WLR_ERROR, "synui: logind: SetBrightness: %s",
                err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);
}

/* ── Event loop ──────────────────────────────────────────── */

static int logind_readable(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    syn_server_t *s = data;

    for (;;) {
        int r = sd_bus_process(lg.bus, NULL);
        if (r > 0) continue;
        if (r == 0) break;
        wlr_log(WLR_ERROR, "synui: logind: bus error: %s — disabling", strerror(-r));
        logind_finish(s);
        return 0;
    }
    sd_bus_flush(lg.bus);
    return 0;
}

void logind_init(syn_server_t *s)
{
    lg.inhibit_fd = -1;
    lg.lid_fd     = -1;
    lg.bl_name[0] = '\0';

    /* login1 is on the system bus. */
    int r = sd_bus_open_system(&lg.bus);
    if (r < 0) {
        wlr_log(WLR_INFO, "synui: logind: no system bus (%s) — no lock-on-sleep,"
                " no brightness keys", strerror(-r));
        lg.bus = NULL;
        return;
    }

    r = sd_bus_match_signal(lg.bus, NULL, LOGIND_SVC, LOGIND_MGR_PATH,
                            LOGIND_MGR_IF, "PrepareForSleep",
                            on_prepare_for_sleep, s);
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: logind: cannot watch PrepareForSleep: %s",
                strerror(-r));
        goto fail;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    lg.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(lg.bus), WL_EVENT_READABLE,
                                  logind_readable, s);
    if (!lg.src) goto fail;

    logind_find_backlight();
    logind_take_inhibitor();
    logind_lid_update(s);

    wlr_log(WLR_INFO, "synui: logind: up (lock-on-sleep%s)",
            lg.bl_name[0] ? ", brightness" : ", no backlight");
    return;

fail:
    sd_bus_unref(lg.bus);
    lg.bus = NULL;
}

void logind_finish(syn_server_t *s)
{
    (void)s;
    /* Drop the inhibitors first: holding the sleep one while shutting down
     * would make logind wait out its full delay on the next sleep for a
     * compositor that is no longer there to lock anything, and holding the lid
     * one would leave a laptop that no longer sleeps when it is shut. */
    logind_drop_inhibitor();
    if (lg.lid_fd >= 0) { close(lg.lid_fd); lg.lid_fd = -1; }
    if (lg.src) { wl_event_source_remove(lg.src); lg.src = NULL; }
    if (lg.bus) { sd_bus_unref(lg.bus); lg.bus = NULL; }
}
