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
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
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

    /* Backlight, resolved once from sysfs. name[0] == 0 means this machine has
     * none (a desktop), and the brightness binds then do nothing. */
    char                    bl_name[64];
    int                     bl_max;
} lg = { .inhibit_fd = -1 };

/* ── Sleep inhibitor ─────────────────────────────────────── */

static void logind_take_inhibitor(void)
{
    if (!lg.bus || lg.inhibit_fd >= 0) return;

    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;

    /* "delay", not "block": block refuses sleep outright. delay just buys us
     * the few seconds needed to get the lock up first. */
    int r = sd_bus_call_method(lg.bus, LOGIND_SVC, LOGIND_MGR_PATH, LOGIND_MGR_IF,
                               "Inhibit", &err, &reply, "ssss",
                               "sleep", "synui", "Lock the session before sleep",
                               "delay");
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: logind: cannot take sleep inhibitor: %s",
                err.message ? err.message : strerror(-r));
        goto out;
    }

    int fd = -1;
    r = sd_bus_message_read(reply, "h", &fd);
    if (r < 0 || fd < 0) {
        wlr_log(WLR_ERROR, "synui: logind: inhibitor reply had no fd");
        goto out;
    }

    /* The fd belongs to the message and is closed with it — dup it, or the
     * inhibitor evaporates the moment this reply is unreffed and the lock race
     * comes back without a single error to show for it. */
    lg.inhibit_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (lg.inhibit_fd < 0)
        wlr_log(WLR_ERROR, "synui: logind: dup inhibitor fd: %s", strerror(errno));

out:
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
}

static void logind_drop_inhibitor(void)
{
    if (lg.inhibit_fd >= 0) { close(lg.inhibit_fd); lg.inhibit_fd = -1; }
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
            synui_spawn(s->config.power_lock_cmd);
        }
        /* Release the delay so the machine can actually sleep. The lock command
         * has been spawned; logind gives us InhibitDelayMaxSec (5s) and we do
         * not need it — swaylock maps and takes the session on its own clock. */
        logind_drop_inhibitor();
    } else {
        wlr_log(WLR_INFO, "synui: logind: resumed");
        logind_take_inhibitor();
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
    /* Drop the inhibitor first: holding it while shutting down would make
     * logind wait out its full delay on the next sleep for a compositor that
     * is no longer there to lock anything. */
    logind_drop_inhibitor();
    if (lg.src) { wl_event_source_remove(lg.src); lg.src = NULL; }
    if (lg.bus) { sd_bus_unref(lg.bus); lg.bus = NULL; }
}
