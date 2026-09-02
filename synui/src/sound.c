/*
 * sound.c — event sounds, and the Super+S panel that turns them on.
 *
 * The desktop is SILENT by default and stays that way until someone asks for a
 * noise: sounds.state does not exist until the first toggle, and with no file
 * every event here returns without doing anything. That is deliberate — a
 * desktop that starts chiming after an upgrade gets its speakers muted, and then
 * the one sound the user did want is gone too.
 *
 * Three pieces:
 *
 *   sound_play()          the rest of the compositor's entry point. Cheap enough
 *                         to call from a hot path: with sounds off it is a stat
 *                         and two compares.
 *   the Super+S panel     master switch, volume, theme, one row per event, and
 *                         `t` to hear a sample without enabling it.
 *   the udev monitor      what makes "USB plugged in" an event at all.
 *
 * synui-sound is the single writer of sounds.state and the thing that actually
 * plays a sample; this file never writes the file and never opens an audio
 * device. What it keeps is a CACHE of the file, refreshed whenever its mtime
 * moves, and the cache can only ever skip a fork — the helper re-reads the state
 * itself, so a stale copy here can delay a sound, never play one that is off.
 *
 * Samples come from the XDG sound theme, so they are the samples every other
 * desktop uses and the user's own theme in ~/.local/share/sounds just works.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <libudev.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "i18n.h"

/* The key in sounds.state, and the argument to `synui-sound play`. Must match
 * the helper's $EVENTS list exactly — this is the whole binding between them. */
const char *sound_event_name(syn_sound_event_t evt)
{
    switch (evt) {
    case SOUND_EVT_LOGIN:          return "login";
    case SOUND_EVT_LOGOUT:         return "logout";
    case SOUND_EVT_DEVICE_ADDED:   return "device_added";
    case SOUND_EVT_DEVICE_REMOVED: return "device_removed";
    case SOUND_EVT_LOCK:           return "lock";
    case SOUND_EVT_UNLOCK:         return "unlock";
    case SOUND_EVT_NOTIFY:         return "notify";
    case SOUND_EVT_SCREENSHOT:     return "screenshot";
    /* volume_change, not volume: `volume` is already the master level's key in
     * the same flat file, and an event of that name read its own line into the
     * level and silenced everything. */
    case SOUND_EVT_VOLUME:         return "volume_change";
    case SOUND_EVT_ERROR:          return "error";
    default:                       return NULL;
    }
}

const char *sound_event_label(syn_sound_event_t evt)
{
    switch (evt) {
    case SOUND_EVT_LOGIN:          return _("Login");
    case SOUND_EVT_LOGOUT:         return _("Logout");
    case SOUND_EVT_DEVICE_ADDED:   return _("Device plugged in");
    case SOUND_EVT_DEVICE_REMOVED: return _("Device unplugged");
    case SOUND_EVT_LOCK:           return _("Screen locked");
    case SOUND_EVT_UNLOCK:         return _("Screen unlocked");
    case SOUND_EVT_NOTIFY:         return _("Notification");
    case SOUND_EVT_SCREENSHOT:     return _("Screenshot");
    case SOUND_EVT_VOLUME:         return _("Volume change");
    case SOUND_EVT_ERROR:          return _("Error / alert");
    default:                       return "?";
    }
}

/* Must match synui-sound's ids() — the primary is the sound-naming spec's name
 * for the event, the rest are what real themes actually ship under. Duplicated
 * here for one reason: so the panel can show which sample an event will play,
 * resolved against the theme on disk. Several spec names (desktop-login,
 * desktop-screen-lock) exist in no installed theme, so printing the primary
 * would name a sample nobody ever hears. */
const char *sound_event_ids(syn_sound_event_t evt)
{
    switch (evt) {
    case SOUND_EVT_LOGIN:          return "desktop-login service-login";
    case SOUND_EVT_LOGOUT:         return "desktop-logout service-logout";
    case SOUND_EVT_DEVICE_ADDED:   return "device-added";
    case SOUND_EVT_DEVICE_REMOVED: return "device-removed";
    case SOUND_EVT_LOCK:           return "desktop-screen-lock bell";
    case SOUND_EVT_UNLOCK:         return "desktop-unlock complete";
    case SOUND_EVT_NOTIFY:         return "message-new-instant message";
    case SOUND_EVT_SCREENSHOT:     return "screen-capture camera-shutter";
    case SOUND_EVT_VOLUME:         return "audio-volume-change";
    case SOUND_EVT_ERROR:          return "dialog-error";
    default:                       return "";
    }
}

/* ── The cached sounds.state ─────────────────────────────── */

static void sound_defaults(syn_sound_t *snd)
{
    snd->enabled = 0;
    snd->volume  = 70;
    for (int i = 0; i < SOUND_EVT_COUNT; i++) {
        snd->on[i] = 0;
        snd->sample[i][0] = '\0';   /* no pick — the automatic chain */
    }
    snprintf(snd->theme, sizeof(snd->theme), "freedesktop");
}

/*
 * Re-read the file if it has moved since the last look. Keying on mtime rather
 * than a reload hook is what lets `synui-sound login on` in a terminal take
 * effect immediately without the compositor being told: there is no reload path
 * to remember to call, and no way for the two to drift.
 *
 * An absent file resets to the defaults — that is `synui-sound` never having
 * been run, or the user deleting it to get the silence back.
 */
void sound_state_refresh(syn_server_t *s)
{
    syn_sound_t *snd = &s->sound;

    char path[256];
    if (!syn_config_path(path, sizeof(path), "sounds.state")) {
        if (!snd->loaded) { sound_defaults(snd); snd->loaded = 1; }
        return;
    }

    /* OPEN FIRST, THEN ASK THE DESCRIPTOR. A stat of the name followed by an
     * fopen of the same name resolves it twice, and `synui-sound` rewrites
     * this file by rename — so the mtime cached here would be the old file's
     * while the settings read are the new one's, and the difference is a
     * change that never takes effect until the next write. */
    FILE *f = fopen(path, "r");
    if (!f) {
        if (snd->mtime != 0 || !snd->loaded) {
            sound_defaults(snd);
            snd->mtime  = 0;
            snd->loaded = 1;
        }
        return;
    }

    struct stat st;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return; }
    if (snd->loaded && snd->mtime == (long)st.st_mtime) { fclose(f); return; }

    sound_defaults(snd);
    snd->mtime  = (long)st.st_mtime;
    snd->loaded = 1;

    char line[192];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = line;
        while (*key == ' ' || *key == '\t') key++;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val);
        while (end > val && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

        if (strcmp(key, "enabled") == 0) {
            snd->enabled = (strcmp(val, "on") == 0);
        } else if (strcmp(key, "volume") == 0) {
            int v = atoi(val);
            snd->volume = v < 0 ? 0 : v > 100 ? 100 : v;
        } else if (strcmp(key, "theme") == 0) {
            snprintf(snd->theme, sizeof(snd->theme), "%s", val);
        } else {
            for (int i = 0; i < SOUND_EVT_COUNT; i++) {
                const char *n = sound_event_name(i);
                if (!n) continue;

                if (strcmp(key, n) == 0) {
                    snd->on[i] = (strcmp(val, "on") == 0);
                    break;
                }

                /* "<event>_sound" — which sample this event plays. Matched by
                 * building the key rather than comparing a prefix: "login" is a
                 * prefix of nothing here today, but an event added later that
                 * extends another's name would silently steal its line. */
                char skey[64];
                snprintf(skey, sizeof(skey), "%s_sound", n);
                if (strcmp(key, skey) == 0) {
                    /* "default"/"auto" is the absence of a pick, not a sample. */
                    if (strcmp(val, "default") == 0 || strcmp(val, "auto") == 0 ||
                        val[0] == '\0')
                        snd->sample[i][0] = '\0';
                    else
                        snprintf(snd->sample[i], SOUND_SAMPLE_MAX, "%s", val);
                    break;
                }
            }
        }
    }
    fclose(f);
}

/* ── Playing ─────────────────────────────────────────────── */

void sound_play(syn_server_t *s, syn_sound_event_t evt)
{
    if (!s || evt < 0 || evt >= SOUND_EVT_COUNT) return;

    sound_state_refresh(s);

    /* The fast path, and the only reason the cache exists: on a silent desktop
     * — the default — an event costs a stat and two compares instead of a fork.
     * synui-sound checks all three of these again for itself, so being wrong
     * here can only ever mean a missed sound, never an unwanted one. */
    if (!s->sound.enabled) return;
    if (!s->sound.on[evt]) return;
    if (s->sound.volume <= 0) return;

    const char *name = sound_event_name(evt);
    if (!name) return;

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "synui-sound play %s", name);
    synui_spawn(cmd);
}

/* ── udev: a device appearing is an event ────────────────── */
/*
 * The reason "USB plugged in" can make a noise at all. Filtered to the usb
 * subsystem's usb_device devtype, which is one event per physical thing plugged
 * in — the unfiltered stream fires once per interface as well, so a keyboard
 * with a hub in it would chime three times.
 *
 * The monitor fd goes on the wl_event_loop like every other watcher in synui
 * (see synapd_mon.c, notif.c): no thread, no poll, and it dies with the display.
 */
/* ── Screen audio ────────────────────────────────────────────
 *
 * Plugging a laptop into a TV and getting no sound out of it is a complaint
 * with a boring cause: the display's audio sink turns up in the graph and
 * nothing makes it the default, because wireplumber ranks the built-in speakers
 * higher and is right to, most of the time.
 *
 * synui is the process that knows a screen just appeared, so it is the one that
 * can say "now". It does not touch PipeWire itself — synui-hdmi-audio(1) owns
 * every part of the decision that needs the graph, and in particular the ELD
 * check that tells a connector with a display on it from the half-dozen dormant
 * HDMI pins every GPU advertises. See that script's header.
 *
 * Resolved per call rather than cached: `auto` depends on the machine, and the
 * user can change the row between one hotplug and the next.
 */
int sound_hdmi_follow_enabled(syn_server_t *s)
{
    if (s->config.hdmi_audio > 0) return 1;
    if (s->config.hdmi_audio == 0) return 0;
    return power_has_battery() ? 1 : 0;
}

void sound_hdmi_follow(syn_server_t *s, int connected)
{
    if (!sound_hdmi_follow_enabled(s)) return;

    /* Same seat guard the font and theme pushes take: the default sink belongs
     * to the seat's session, and a nested or headless synui shares the user's
     * PipeWire graph with the desktop it is running inside. A test instance
     * moving the real desktop's audio is precisely the class of accident the
     * guard exists for. */
    if (!synui_owns_seat(s)) return;

    /* The helper blocks for up to 8s waiting for the sink to appear — it must
     * not run on the compositor's thread, and synui_spawn() is a fork+exec that
     * does not wait. */
    synui_spawn(connected ? "synui-hdmi-audio follow"
                          : "synui-hdmi-audio restore");
}

static int udev_readable(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    syn_server_t *s = data;

    struct udev_device *dev;
    /* Drain, don't handle one: several devices can land in one wakeup, and a
     * receive that is not drained leaves the fd readable and spins the loop. */
    while ((dev = udev_monitor_receive_device(s->udev_mon)) != NULL) {
        const char *action = udev_device_get_action(dev);
        if (action) {
            if (strcmp(action, "add") == 0)
                sound_play(s, SOUND_EVT_DEVICE_ADDED);
            else if (strcmp(action, "remove") == 0)
                sound_play(s, SOUND_EVT_DEVICE_REMOVED);
        }
        udev_device_unref(dev);
    }
    return 0;
}

void sound_udev_init(syn_server_t *s)
{
    /* Everything here is optional. A compositor that will not start because it
     * could not open a udev socket would be a far worse trade than a desktop
     * with no plug-in chime. */
    s->udev = udev_new();
    if (!s->udev) {
        wlr_log(WLR_INFO, "synui: sound: no udev — device sounds unavailable");
        return;
    }

    s->udev_mon = udev_monitor_new_from_netlink(s->udev, "udev");
    if (!s->udev_mon) {
        wlr_log(WLR_INFO, "synui: sound: no udev monitor — device sounds unavailable");
        udev_unref(s->udev);
        s->udev = NULL;
        return;
    }

    udev_monitor_filter_add_match_subsystem_devtype(s->udev_mon, "usb", "usb_device");
    /* Storage that is not on USB (an SD card reader, a hot-plugged SATA disk)
     * arrives on the block subsystem instead, and "a disk appeared" is the same
     * event to a user. Whole disks only: a stick with four partitions is one
     * thing plugged in, not five. */
    udev_monitor_filter_add_match_subsystem_devtype(s->udev_mon, "block", "disk");
    udev_monitor_enable_receiving(s->udev_mon);

    int fd = udev_monitor_get_fd(s->udev_mon);
    if (fd < 0) {
        sound_udev_finish(s);
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->udev_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                                       udev_readable, s);
    if (!s->udev_src) {
        sound_udev_finish(s);
        return;
    }
    wlr_log(WLR_INFO, "synui: sound: udev device monitor up");
}

void sound_udev_finish(syn_server_t *s)
{
    if (s->udev_src) { wl_event_source_remove(s->udev_src); s->udev_src = NULL; }
    if (s->udev_mon) { udev_monitor_unref(s->udev_mon);     s->udev_mon = NULL; }
    if (s->udev)     { udev_unref(s->udev);                 s->udev     = NULL; }
}

/* ── Panel ───────────────────────────────────────────────── */

#define SOUND_VOL_STEP 5

/* Every change goes through synui-sound, which is the single writer, and is
 * mirrored into the cache immediately: re-reading the file after the spawn would
 * race the child and draw the previous value under the cursor.
 *
 * The mtime is deliberately LEFT ALONE. The next refresh re-reads only if the
 * file has moved — which, once the child has run, it has, and the value it finds
 * is the one already on screen. If the child has not run yet the mtime is
 * unchanged, no read happens, and the optimistic value stands. Zeroing it here
 * (the first cut) forced an unconditional re-read and so reintroduced exactly
 * the race the optimism exists to avoid. */
static void sound_apply(syn_server_t *s, const char *cmd)
{
    synui_spawn(cmd);
}

static void sound_set_event(syn_server_t *s, int evt, int on)
{
    s->sound.on[evt] = on;
    /* Enabling any one event implies wanting sound at all, exactly as
     * synui-sound does it — otherwise the first thing anyone turns on is
     * followed by silence and a hunt for the master switch. */
    if (on) s->sound.enabled = 1;

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "synui-sound %s %s",
             sound_event_name(evt), on ? "on" : "off");
    sound_apply(s, cmd);

    snprintf(s->sound.status, sizeof(s->sound.status),
             on ? _("%s on") : _("%s off"), sound_event_label(evt));
}

static void sound_set_master(syn_server_t *s, int on)
{
    s->sound.enabled = on;
    sound_apply(s, on ? "synui-sound master on" : "synui-sound master off");
    snprintf(s->sound.status, sizeof(s->sound.status),
             "event sounds %s", on ? "on" : "off");
}

static void sound_set_volume(syn_server_t *s, int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    if (vol == s->sound.volume) return;
    s->sound.volume = vol;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-sound volume %d", vol);
    sound_apply(s, cmd);

    if (vol == 0)
        snprintf(s->sound.status, sizeof(s->sound.status), "%s",
                 _("volume 0% \xc2\xb7 muted"));
    else
        snprintf(s->sound.status, sizeof(s->sound.status), _("volume %d%%"), vol);
}

/* The theme row cycles whatever is installed. Reading the directory listing here
 * rather than shelling out keeps the panel's redraw synchronous — the list is
 * two or three entries and it is only walked on a keypress. */
#define SOUND_THEMES_MAX  24
#define SOUND_SAMPLES_MAX 256

/* Same test synui-sound's is_theme() makes, and for the same reason:
 * /usr/share/sounds/alsa is nine channel-test .wav files dropped there by
 * alsa-utils, with no index.theme and no output-profile directory. It was being
 * offered in the picker, and selecting it silenced every event — it holds no XDG
 * sample id at all — with nothing on screen to say why. */
static int sound_dir_is_theme(const char *dir)
{
    char p[512];
    struct stat st;

    snprintf(p, sizeof(p), "%s/index.theme", dir);
    if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) return 1;
    snprintf(p, sizeof(p), "%s/stereo", dir);
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    snprintf(p, sizeof(p), "%s/mono", dir);
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return 0;
}

/* The three share/sounds directories, user's own first — the same search path,
 * in the same order, that synui-sound looks along. Written into the caller's
 * buffer because the first entry is built from the environment. */
#define SOUND_BASES 3

static void sound_search_path(char home_base[256], const char *bases[SOUND_BASES])
{
    home_base[0] = '\0';
    const char *data_home = getenv("XDG_DATA_HOME");
    const char *home      = getenv("HOME");
    if (data_home && *data_home)
        snprintf(home_base, 256, "%s/sounds", data_home);
    else if (home && *home)
        snprintf(home_base, 256, "%s/.local/share/sounds", home);

    bases[0] = home_base;
    bases[1] = "/usr/local/share/sounds";
    bases[2] = "/usr/share/sounds";
}

static int sound_themes_list(char names[SOUND_THEMES_MAX][SOUND_THEME_MAX])
{
    char home_base[256];
    const char *bases[SOUND_BASES];
    sound_search_path(home_base, bases);
    int n = 0;

    for (size_t b = 0; b < SOUND_BASES; b++) {
        if (!bases[b] || !*bases[b]) continue;

        DIR *d = opendir(bases[b]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < SOUND_THEMES_MAX) {
            if (e->d_name[0] == '.') continue;

            char full[512];
            snprintf(full, sizeof(full), "%s/%s", bases[b], e->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            /* Skipped rather than truncated: a clipped name would be offered in
             * the picker and then rejected by synui-sound, which validates the
             * name against the same directories. No real theme is this long. */
            if (strlen(e->d_name) >= SOUND_THEME_MAX) continue;
            if (!sound_dir_is_theme(full)) continue;

            int dup = 0;
            for (int i = 0; i < n; i++)
                if (strcmp(names[i], e->d_name) == 0) { dup = 1; break; }
            if (dup) continue;

            memcpy(names[n], e->d_name, strlen(e->d_name) + 1);
            n++;
        }
        closedir(d);
    }
    return n;
}

/*
 * Is the selected theme one the picker would offer? Not the same as "the
 * directory exists": a state file written before the ghost-theme filter existed
 * can name /usr/share/sounds/alsa, which is nine channel-test .wav files and no
 * sample ids — every event on, and total silence, with nothing to explain it.
 * Dropping it from the cycle list stops anyone ARRIVING there; this is what
 * tells the people already there what is wrong.
 */
int sound_theme_installed(const char *theme)
{
    static char names[SOUND_THEMES_MAX][SOUND_THEME_MAX];
    int n = sound_themes_list(names);
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], theme) == 0) return 1;
    return 0;
}

static void sound_cycle_theme(syn_server_t *s, int dir)
{
    char names[SOUND_THEMES_MAX][SOUND_THEME_MAX];
    int n = sound_themes_list(names);
    if (n <= 0) {
        snprintf(s->sound.status, sizeof(s->sound.status),
                 "no sound themes installed under /usr/share/sounds");
        return;
    }

    int cur = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], s->sound.theme) == 0) { cur = i; break; }

    cur = (cur + dir + n) % n;
    snprintf(s->sound.theme, sizeof(s->sound.theme), "%s", names[cur]);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "synui-sound theme %s", names[cur]);
    sound_apply(s, cmd);

    snprintf(s->sound.status, sizeof(s->sound.status),
             "theme: %s \xc2\xb7 t to hear it", names[cur]);
}

/* ── Which sample an event plays ─────────────────────────── */

/* Does the theme ship this sample? Only the layout every real theme uses is
 * checked (stereo/, mono/, the theme root); libcanberra does the full spec
 * lookup, inheritance included, and this does not pretend to. Being wrong here
 * costs a label in the panel, never a sound: the helper resolves it again. */
static int sound_sample_exists(const char *theme, const char *id)
{
    static const char *subs[] = { "stereo", "mono", "." };
    static const char *exts[] = { "oga", "ogg", "wav" };

    char home_base[256];
    const char *bases[SOUND_BASES];
    sound_search_path(home_base, bases);

    for (size_t b = 0; b < SOUND_BASES; b++) {
        if (!bases[b] || !*bases[b]) continue;
        for (size_t su = 0; su < sizeof(subs) / sizeof(subs[0]); su++) {
            for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
                char p[640];
                snprintf(p, sizeof(p), "%s/%s/%s/%s.%s",
                         bases[b], theme, subs[su], id, exts[e]);
                struct stat st;
                if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) return 1;
            }
        }
    }
    return 0;
}

/*
 * The sample this event will actually play, for display. A picked one is shown
 * as-is. Otherwise the automatic chain is walked and the first id the theme has
 * on disk wins — which is the honest answer and not the same as the first id in
 * the chain: desktop-login is the spec's name for the login sound and is in no
 * installed theme, so a panel printing it would name a sample nobody hears.
 *
 * Returns 0 when nothing in the chain exists. That is a real state — a theme
 * that simply has no sound for this event — and the panel says so, because
 * "enabled but silent" is otherwise indistinguishable from a broken toggle.
 */
int sound_resolved_id(const syn_sound_t *snd, int evt, char *out, size_t outsz)
{
    if (snd->sample[evt][0]) {
        snprintf(out, outsz, "%s", snd->sample[evt]);
        return sound_sample_exists(snd->theme, snd->sample[evt]);
    }

    const char *chain = sound_event_ids(evt);
    char first[SOUND_SAMPLE_MAX] = "";

    while (*chain) {
        const char *sp = strchr(chain, ' ');
        size_t len = sp ? (size_t)(sp - chain) : strlen(chain);
        if (len >= SOUND_SAMPLE_MAX) len = SOUND_SAMPLE_MAX - 1;

        char id[SOUND_SAMPLE_MAX];
        memcpy(id, chain, len);
        id[len] = '\0';
        if (!first[0]) snprintf(first, sizeof(first), "%s", id);

        if (sound_sample_exists(snd->theme, id)) {
            snprintf(out, outsz, "%s", id);
            return 1;
        }
        if (!sp) break;
        chain = sp + 1;
    }

    /* Nothing on disk: name the one it would have wanted, and report the miss. */
    snprintf(out, outsz, "%s", first);
    return 0;
}

static int sound_samples_list(const char *theme,
                              char ids[SOUND_SAMPLES_MAX][SOUND_SAMPLE_MAX])
{
    static const char *subs[] = { "stereo", "mono", "." };

    char home_base[256];
    const char *bases[SOUND_BASES];
    sound_search_path(home_base, bases);
    int n = 0;

    for (size_t b = 0; b < SOUND_BASES && n < SOUND_SAMPLES_MAX; b++) {
        if (!bases[b] || !*bases[b]) continue;
        for (size_t su = 0; su < sizeof(subs) / sizeof(subs[0]); su++) {
            char dir[512];
            snprintf(dir, sizeof(dir), "%s/%s/%s", bases[b], theme, subs[su]);

            DIR *d = opendir(dir);
            if (!d) continue;
            struct dirent *e;
            while ((e = readdir(d)) != NULL && n < SOUND_SAMPLES_MAX) {
                if (e->d_name[0] == '.') continue;

                const char *dot = strrchr(e->d_name, '.');
                if (!dot) continue;
                if (strcmp(dot, ".oga") != 0 && strcmp(dot, ".ogg") != 0 &&
                    strcmp(dot, ".wav") != 0) continue;

                size_t len = (size_t)(dot - e->d_name);
                if (len == 0 || len >= SOUND_SAMPLE_MAX) continue;

                char id[SOUND_SAMPLE_MAX];
                memcpy(id, e->d_name, len);
                id[len] = '\0';

                /* The same id can be in two of the three bases, and in both
                 * .oga and .wav. The picker must offer it once. */
                int dup = 0;
                for (int i = 0; i < n; i++)
                    if (strcmp(ids[i], id) == 0) { dup = 1; break; }
                if (dup) continue;

                memcpy(ids[n], id, len + 1);
                n++;
            }
            closedir(d);
        }
    }
    return n;
}

static int sound_id_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/*
 * [ and ] step the selected event through the theme's samples. The list is
 * sorted so the two keys are inverses of each other and a second pass lands in
 * the same place — readdir order is neither stable nor alphabetical, and a
 * picker that walks a different sequence each time it is opened is unusable.
 *
 * Position 0 is "auto", the absence of a pick, so backing off a choice is one
 * keypress rather than a trip to the terminal.
 */
static void sound_cycle_sample(syn_server_t *s, int evt, int dir)
{
    /* static: 16K of ids is more than belongs on the stack, and this only ever
     * runs on the main thread from the key handler. */
    static char ids[SOUND_SAMPLES_MAX][SOUND_SAMPLE_MAX];
    int n = sound_samples_list(s->sound.theme, ids);
    if (n <= 0) {
        snprintf(s->sound.status, sizeof(s->sound.status),
                 "theme %s ships no samples", s->sound.theme);
        return;
    }
    qsort(ids, (size_t)n, SOUND_SAMPLE_MAX, sound_id_cmp);

    /* 0 = auto, 1..n = ids[0..n-1]. */
    int cur = 0;
    if (s->sound.sample[evt][0]) {
        for (int i = 0; i < n; i++)
            if (strcmp(ids[i], s->sound.sample[evt]) == 0) { cur = i + 1; break; }
    }

    cur = (cur + dir + (n + 1)) % (n + 1);

    char cmd[192];
    if (cur == 0) {
        s->sound.sample[evt][0] = '\0';
        snprintf(cmd, sizeof(cmd), "synui-sound sound %s default",
                 sound_event_name(evt));
    } else {
        snprintf(s->sound.sample[evt], SOUND_SAMPLE_MAX, "%s", ids[cur - 1]);
        snprintf(cmd, sizeof(cmd), "synui-sound sound %s %s",
                 sound_event_name(evt), ids[cur - 1]);
    }
    sound_apply(s, cmd);

    char shown[SOUND_SAMPLE_MAX];
    sound_resolved_id(&s->sound, evt, shown, sizeof(shown));
    snprintf(s->sound.status, sizeof(s->sound.status),
             _("%s: %s%s \xc2\xb7 t to hear it"), sound_event_label(evt), shown,
             cur == 0 ? " (automatic)" : "");
}

void sound_show(syn_server_t *s)
{
    /* Force a read: the panel is the one place that must show what is on disk,
     * not what the cache last saw. */
    s->sound.mtime  = 0;
    s->sound.loaded = 0;
    sound_state_refresh(s);

    s->sound.visible   = 1;
    s->sound.selected  = SOUND_ROW_EVENT;   /* the first event, not the master */
    s->sound.status[0] = '\0';
    synui_render_sound(s);
}

void sound_hide(syn_server_t *s)
{
    s->sound.visible = 0;
    synui_render_sound(s);
    ctlpanel_child_closed(s, "sounds");
}

void sound_toggle(syn_server_t *s)
{
    if (s->sound.visible) sound_hide(s);
    else                  sound_show(s);
}

static void sound_adjust(syn_server_t *s, int dir)
{
    int row = s->sound.selected;

    if (row == SOUND_ROW_ENABLED) { sound_set_master(s, dir > 0); return; }
    if (row == SOUND_ROW_VOLUME)  { sound_set_volume(s, s->sound.volume + dir * SOUND_VOL_STEP); return; }
    if (row == SOUND_ROW_THEME)   { sound_cycle_theme(s, dir); return; }

    int evt = row - SOUND_ROW_EVENT;
    if (evt >= 0 && evt < SOUND_EVT_COUNT)
        sound_set_event(s, evt, dir > 0);
}

static void sound_activate(syn_server_t *s)
{
    int row = s->sound.selected;

    if (row == SOUND_ROW_ENABLED) { sound_set_master(s, !s->sound.enabled); return; }
    if (row == SOUND_ROW_VOLUME || row == SOUND_ROW_THEME) {
        /* Neither is a switch; Enter previews instead of doing nothing. */
        synui_spawn("synui-sound test login");
        snprintf(s->sound.status, sizeof(s->sound.status),
                 "playing a sample from %s", s->sound.theme);
        return;
    }

    int evt = row - SOUND_ROW_EVENT;
    if (evt >= 0 && evt < SOUND_EVT_COUNT)
        sound_set_event(s, evt, !s->sound.on[evt]);
}

/* `t` plays the selected event's sample whatever its switch says. Hearing what
 * you are about to enable is most of the point of a sound panel, and it is also
 * the only way to tell "this event is off" apart from "this theme has no such
 * sample" or "the audio stack is broken". */
static void sound_test(syn_server_t *s)
{
    int evt = s->sound.selected - SOUND_ROW_EVENT;
    if (evt < 0 || evt >= SOUND_EVT_COUNT) evt = SOUND_EVT_LOGIN;

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "synui-sound test %s", sound_event_name(evt));
    synui_spawn(cmd);

    if (s->sound.volume <= 0)
        snprintf(s->sound.status, sizeof(s->sound.status),
                 "%s \xc2\xb7 volume is 0%%, nothing will be heard",
                 sound_event_label(evt));
    else
        snprintf(s->sound.status, sizeof(s->sound.status),
                 "playing %s", sound_event_label(evt));
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. Enter acts on the row here, so a
 * left click is Enter. A right click is Left, which is what makes the volume row
 * usable with a mouse at all: it is a slider, and a slider you can only push one
 * way is not a slider. */

int sound_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->sound.visible) return 0;

    int row = hit_row_at(&s->sound.hit, lx, ly);
    if (row < 0 || row == s->sound.selected) return 1;
    s->sound.selected = row;
    synui_render_sound(s);
    return 1;
}

int sound_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    if (!s->sound.visible) return 0;

    if (!hit_in_panel(&s->sound.hit, lx, ly)) {
        sound_hide(s);
        return 1;
    }

    sound_motion(s, lx, ly);

    if (hit_row_at(&s->sound.hit, lx, ly) < 0) return 1;   /* chrome */
    if (button != BTN_LEFT && button != BTN_RIGHT) return 1;

    /* Same refresh the key path does first: the volume row adjusts RELATIVELY,
     * so a stale copy would not merely display the old number, it would write it
     * back over a newer one. */
    sound_state_refresh(s);

    if (button == BTN_RIGHT) sound_adjust(s, -1);
    else                     sound_activate(s);

    synui_render_sound(s);
    return 1;
}

int sound_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->sound.visible) return 0;
    if (delta == 0) return 1;

    int next = s->sound.selected + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= SOUND_ROW_COUNT) return 1;
    s->sound.selected = next;
    synui_render_sound(s);
    return 1;
}

int sound_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->sound.visible) return 0;

    /* Before acting. The volume row adjusts s->sound.volume RELATIVELY, so a
     * copy left stale by a `synui-sound volume` in a terminal would not just
     * display the old number — it would write it back over the new one. */
    sound_state_refresh(s);

    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        sound_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->sound.selected > 0) s->sound.selected--;
        synui_render_sound(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->sound.selected < SOUND_ROW_COUNT - 1) s->sound.selected++;
        synui_render_sound(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        sound_adjust(s, -1);
        synui_render_sound(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        sound_adjust(s, +1);
        synui_render_sound(s);
        return 1;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        sound_activate(s);
        synui_render_sound(s);
        return 1;
    case XKB_KEY_space:
        /* The master switch from any row, as Space is in filters.c. */
        sound_set_master(s, !s->sound.enabled);
        synui_render_sound(s);
        return 1;
    case XKB_KEY_t:
        sound_test(s);
        synui_render_sound(s);
        return 1;
    /* [ and ] pick which sample the selected event plays. Unshifted on the
     * layouts this ships on, which matters: the guard above drops anything with
     * a modifier, so Shift+Left would never reach here. */
    case XKB_KEY_bracketleft:
    case XKB_KEY_bracketright: {
        int evt = s->sound.selected - SOUND_ROW_EVENT;
        if (evt < 0 || evt >= SOUND_EVT_COUNT) {
            snprintf(s->sound.status, sizeof(s->sound.status),
                     "[ ] picks a sound \xc2\xb7 select an event row first");
        } else {
            sound_cycle_sample(s, evt, sym == XKB_KEY_bracketright ? +1 : -1);
        }
        synui_render_sound(s);
        return 1;
    }
    default:
        return 1;   /* modal */
    }
}
