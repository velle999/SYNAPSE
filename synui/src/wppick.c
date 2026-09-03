/*
 * wppick.c — wallpaper selector panel
 *
 * A compositor-drawn modal picker (Super+W, or "wallpaper" bind) for
 * switching wallpaper without editing synuirc:
 *
 *   Up/Down (j/k)     move the highlight (applies live for instant preview)
 *   Tab               cycle the scope: all monitors, or one of them
 *   m                 cycle the scaling mode
 *   Enter / Esc / q   close
 *   r                 rescan for images
 *
 * The scope is what makes per-monitor wallpapers reachable without editing
 * synuirc: with it on "All monitors" a pick sets the global wallpaper and drops
 * every per-monitor override, and with it on a connector the pick becomes that
 * monitor's override alone (config.wallpaper_out[], see wallpaper.c). The
 * scaling mode follows the same scope, so one screen can fill while another
 * fits.
 *
 * Below the built-ins, the panel lists image files found in the usual wallpaper
 * directories (~/Pictures and friends, /usr/share/backgrounds) — that is the
 * "browse" option. synui has no file chooser and no text entry, so pointing it
 * at a directory the user already keeps images in beats either building one or
 * shelling out to a GTK dialog for one path. The scan re-runs each time the
 * panel opens, so an image dropped into ~/Pictures shows up without a restart.
 *
 * Selecting an entry applies it immediately (so you see the change while the
 * panel is still open) and persists it to ~/.config/synui/wallpaper.state, so
 * the choice survives a restart. The persisted choice overrides the synuirc
 * `wallpaper` line on the next load — delete the state file to hand control
 * back to synuirc.
 *
 * The panel itself follows dispcfg.c's modal pattern: state in the server
 * struct, a wlr_scene tree drawn by synui_render_wppick() (render.c), and a
 * key handler that swallows input while open.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "i18n.h"

/* Built-in wallpapers offered by the picker. Order is the on-screen order.
 *
 * ⚠ THE LAST ONE IS NOT A WALLPAPER. "Wallhaven" opens the browser instead of
 * painting anything — where the other rows answer "which picture", it answers
 * "I do not want any of these". It sits at the bottom of the built-ins, above
 * the scan, because a row that leaves the panel does not belong among the ones
 * that change it. See wppick_is_action() for what stops it behaving like a
 * picture. */
const struct wppick_option wppick_options[] = {
    { "Synapse",   "Default image wallpaper",       "default"   },
    { "Matrix",    "Animated kanji rain (GPU)",     "matrix"    },
    { "None",      "Solid background color",        "none"      },
    { "Wallhaven", "Browse wallhaven.cc for more",  "wallhaven" },
};
const int wppick_option_count =
    (int)(sizeof(wppick_options) / sizeof(wppick_options[0]));

/* Defined down with the Workshop scan, but the apply/preview paths above it
 * need to tell a Workshop row from an image row. */
static int wppick_we_index(syn_server_t *s, int row);

/*
 * Open the wallhaven browser.
 *
 * ⚠ THE LAUNCHER AND NOT quickshell DIRECTLY. synui-wallhaven owns which tree
 * the QML comes from and the toggle across a process boundary; the window owns
 * the network switch, and says so on its own face when it is off. Super+Ctrl+W
 * (input.c) spawns the identical command.
 *
 * ⛔ THE CALLER CLOSES THE PANEL. The browser wants the keyboard, and two
 * full-screen surfaces both asking for it is a panel nobody can drive — but the
 * row path commits from inside wppick_apply(), whose callers close immediately
 * afterwards, so a wppick_hide() here would be one close too many.
 */
static void wppick_wallhaven_open(void)
{
    synui_spawn("synui-wallhaven toggle");
}

/* The Wallhaven row's index, which is also where the header button takes its
 * label from — one spelling, so the button and the row cannot drift apart. */
int wppick_wallhaven_row(void)
{
    for (int i = 0; i < wppick_option_count; i++)
        if (strcmp(wppick_options[i].token, "wallhaven") == 0) return i;
    return -1;
}

/*
 * Is this row an ACTION rather than a wallpaper?
 *
 * ⛔ THE PANEL APPLIES ON HIGHLIGHT. Moving onto a row sets that wallpaper so
 * you see it while the panel is still open — which is the whole reason the
 * picker feels the way it does, and which would make merely SCROLLING PAST the
 * Wallhaven row open a network browser. So an action row does nothing at all
 * until Enter, and the preview path checks this first.
 */
static bool wppick_is_action(syn_server_t *s, int idx)
{
    if (idx < 0 || idx >= wppick_option_count) return false;
    (void)s;
    return idx == wppick_wallhaven_row();
}

/* ── Scope ───────────────────────────────────────────────── */

/* The connector the panel is currently pointed at, or NULL for "all
 * monitors". */
const char *wppick_scope_output(syn_server_t *s)
{
    if (s->wppick.scope < 0 || s->wppick.scope >= s->wppick.out_count)
        return NULL;
    return s->wppick.out[s->wppick.scope];
}

/* Human-readable scope for the panel's title row. */
const char *wppick_scope_label(syn_server_t *s)
{
    const char *out = wppick_scope_output(s);
    return out ? out : "All monitors";
}

/* Snapshot the connected outputs. One entry per monitor, in layout order —
 * which is the order the outputs list is in, so Tab walks the screens the way
 * they are arranged rather than in hotplug order. */
static void wppick_scan_outputs(syn_server_t *s)
{
    s->wppick.out_count = 0;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (s->wppick.out_count >= SYN_WP_PEROUT_MAX) break;
        if (!o->wlr_output->name) continue;
        snprintf(s->wppick.out[s->wppick.out_count],
                 sizeof(s->wppick.out[0]), "%s", o->wlr_output->name);
        s->wppick.out_count++;
    }
}

/* The wallpaper the current scope shows. Under "all monitors" that is the
 * global config; under a connector it is that monitor's effective wallpaper,
 * so opening the panel on a screen with its own picture highlights that
 * picture. */
static void wppick_scope_state(syn_server_t *s, syn_wallpaper_src_t *src,
                               const char **path)
{
    wallpaper_effective(&s->config, wppick_scope_output(s), src, path, NULL);
}

/* Which option currently matches the live config, so the panel opens with the
 * active wallpaper highlighted. */
/* The id synui-wpengine currently has applied, or "" when it is not running.
 * synui does not mirror that state — the script owns it — so the picker reads
 * it back rather than trusting a copy that a `synui-wpengine off` from a
 * terminal would have made stale. */
static void wppick_active_we(char *out, size_t n, const char *scope)
{
    out[0] = '\0';

    const char *home = getenv("HOME");
    if (!home || !*home) return;

    char path[256];
    if (snprintf(path, sizeof(path), "%s/.config/synui/wpengine.state", home)
            >= (int)sizeof(path))
        return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Every line is "<output> <id>". A monitor scope wants that monitor's
     * line; "all monitors" has no one line to point at, so the first one
     * stands in — which is exactly right when they all match and is the only
     * defensible guess when they do not. */
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char name[64], id[24];
        if (sscanf(line, "%63s %23s", name, id) != 2) continue;
        if (scope && strcmp(name, scope) != 0) continue;
        snprintf(out, n, "%s", id);
        break;
    }
    fclose(f);
}

/* ── Re-arming the engine after a suspend or a monitor change ────────── */
/*
 * linux-wallpaperengine cannot survive losing an output. Its registry
 * global_remove handler is literally `// todo: outputs` (a no-op), and
 * onLayerClose() frees a viewport's EGL surface, layer surface and wl_surface
 * and erases it from m_screens with nothing that ever rebuilds it — while
 * setupOutputLayerSurfaces(), the only caller of setupLS(), runs once at init.
 * A wl_output that appears later does get a viewport, but never a surface.
 *
 * So when a suspend/resume takes an output away and brings it back, the engine
 * process stays alive, ends up with no layer surfaces at all, and blocks in
 * poll() at 0% CPU forever. What is left on screen is whatever wallpaper.c
 * paints underneath, which reads as "the wallpaper did not come back from
 * standby" — and nothing short of restarting the engine fixes it, because
 * there is no code path in it that can.
 *
 * synui cannot repair the client, so it restarts it. The trigger is
 * deliberately not "an output was recreated": the surfaces can also be lost
 * without synui destroying anything (the compositor is not the only thing a
 * GPU suspend disturbs), so the resume itself arms this too — see logind.c.
 * Both funnel through one timer, which is what keeps three monitors coming
 * back at once from meaning three engine restarts.
 */

/* Long enough for a DRM re-probe to finish adding the connectors back, so
 * `synui-wpengine restore` sees the full output list: build_args drops any
 * state line naming an output synctl cannot see, and a restore that ran while
 * two of three monitors were still missing would helpfully persist that. Each
 * trigger re-arms, so this is measured from the LAST one, not the first. */
#define WPENGINE_RESTORE_DELAY_MS 2500

static int wpengine_restore_cb(void *data)
{
    syn_server_t *s = data;

    /* Re-checked at fire time rather than only at arm time: a `synui-wpengine
     * off` in the window between the two means there is no longer an engine to
     * bring back, and restarting one would put a wallpaper back that the user
     * just took away. */
    char id[24];
    wppick_active_we(id, sizeof(id), NULL);
    if (!id[0]) return 0;

    wlr_log(WLR_INFO, "synui: wpengine: restoring the Workshop wallpaper "
            "(the engine cannot re-create its own layer surfaces)");
    synui_spawn("synui-wpengine restore");
    return 0;
}

void wpengine_restore_soon(syn_server_t *s)
{
    if (!s || !s->display) return;

    /* No Workshop wallpaper configured — the overwhelmingly common case, and
     * the one where spawning a shell script per resume would be pure noise.
     * NULL scope takes the first line: any line at all means the engine is
     * meant to be up, and `restore` re-applies every one of them anyway. */
    char id[24];
    wppick_active_we(id, sizeof(id), NULL);
    if (!id[0]) return;

    if (!s->wpengine.timer) {
        s->wpengine.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(s->display), wpengine_restore_cb, s);
        if (!s->wpengine.timer) return;
    }
    /* Re-arming an armed timer replaces the deadline, which is the coalescing. */
    wl_event_source_timer_update(s->wpengine.timer, WPENGINE_RESTORE_DELAY_MS);
}

/*
 * wpengine_note_before_sleep — say who holds the GPU. It used to KILL them.
 *
 * ── Why this stopped killing, 2026-08-06 ────────────────────────────────────
 *
 * The driver handles it now. systemd 256 began freezing user sessions on sleep,
 * which does not work with the NVIDIA drivers; systemd's guidance is that
 * NVIDIA packagers ship a drop-in setting SYSTEMD_SLEEP_FREEZE_USER_SESSIONS=
 * false, and nvidia-utils does, as of 610.43.03 dated 2026-07-28 — the day
 * before this function was written, against a package that did not have it yet.
 * Six suspends since, no freeze failures, including one with two CUDA holders
 * open that froze in 0.001s.
 *
 * And killing a CUDA holder immediately before suspend is HOW YOU CREATE the
 * failure below. The unfreezable task on 2026-07-28 was caught EXITING inside
 * uvm_va_space_mm_shutdown; a process nobody is killing is not in teardown.
 * Bounding the wait at 2.3s and then releasing the inhibitor anyway meant
 * suspending with an engine possibly still inside the driver — the exact state
 * this was written to prevent. It was a deadline dressed up as the absence of
 * one.
 *
 * Caveat, so the paragraph above is not over-read: the drop-in governs
 * systemd's session-cgroup freeze, and the abort below was the KERNEL PM
 * freezer. Different mechanism; they interact; the evidence is not a proof.
 *
 * Nothing else needs doing on resume either way — wpengine_restore_soon() is
 * armed from logind.c unconditionally, and the state file is untouched.
 *
 * IF SUSPEND REGRESSES, the signalling is one revert away — but get the
 * "Freezing user space processes failed ... refusing to freeze" line naming the
 * task first. Without it this is not the thing to change.
 *
 * ── The original hazard, kept because it is what to look for ────────────────
 *
 * linux-wallpaperengine holds a CUDA context (it decodes video wallpapers on
 * the GPU through an embedded mpv). Left running into a suspend it can be
 * caught exiting inside nvidia_uvm's teardown, where it is unfreezable: on
 * 2026-07-28 one of these held `uvm_va_space_mm_shutdown` until the kernel gave
 * up — "Freezing user space processes failed after 20.004 seconds (1 tasks
 * refusing to freeze)" — which ABORTED the suspend half-completed. The NVIDIA
 * driver came back with inconsistent state for one head, could no longer
 * register surfaces for it ("Invalid request parameters, planePitch or
 * rmObjectSizeInBytes"), and DP-3 spent the next day scanning out a frozen
 * console while its connector read `disconnected` with a 0-byte EDID. A reboot
 * was the only way back.
 *
 * That is what a genuine freeze failure looks like, and what to match against
 * the journal before blaming anything here.
 */

/* Linux truncates comm to 15 chars, so the binary shows up shortened. The
 * helper script hardcodes the same string for the same reason. */
#define WPENGINE_COMM "linux-wallpaper"

/* Signal every engine; returns how many were still alive to be signalled.
 * sig 0 just counts them. */
static int wpengine_signal_all(int sig)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;

    int hit = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;

        char path[64], comm[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(comm, sizeof(comm), f)) { fclose(f); continue; }
        fclose(f);
        comm[strcspn(comm, "\n")] = '\0';
        if (strcmp(comm, WPENGINE_COMM) != 0) continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid <= 1) continue;
        if (sig == 0 || kill(pid, sig) == 0) hit++;
    }
    closedir(d);
    return hit;
}

void wpengine_note_before_sleep(void)
{
    int n = wpengine_signal_all(0);
    if (!n) return;   /* no Workshop wallpaper running — the common case */

    /* The whole job now. If a suspend ever does fail to freeze again, this line
     * is already in the journal saying how many engines were live when it
     * started — which beats reconstructing it from a kernel stack trace. */
    wlr_log(WLR_INFO, "synui: wpengine: %d engine(s) hold a CUDA context into "
            "this suspend (not stopped: the driver's no-freeze-session drop-in "
            "covers this, and killing them here is what puts one in nvidia_uvm "
            "teardown at exactly the wrong moment)", n);
}

void wpengine_output_lost(syn_server_t *s)
{
    if (s) s->wpengine.lost_output = 1;
}

void wpengine_output_added(syn_server_t *s)
{
    /* Every output fires this at startup, where synuirc's autostart line is
     * already running `synui-wpengine restore` — a second one would stop the
     * engine it just started and flash the desktop. Only an output that comes
     * back AFTER one went away can have cost the engine a surface. */
    if (s && s->wpengine.lost_output) wpengine_restore_soon(s);
}

static int current_index(syn_server_t *s)
{
    /* An engine wallpaper wins over anything wallpaper.c holds: its surface is
     * what is actually on screen. Checked against the state file rather than
     * wallpaper_src so a choice made before this synui started still lands. */
    char active[24];
    wppick_active_we(active, sizeof(active), wppick_scope_output(s));
    if (active[0]) {
        for (int i = 0; i < s->wppick.we_count; i++)
            if (strcmp(s->wppick.we[i].id, active) == 0)
                return wppick_option_count + s->wppick.found_count + i;
    }

    syn_wallpaper_src_t src;
    const char *path;
    wppick_scope_state(s, &src, &path);

    if (src == SYN_WP_SRC_MATRIX)
        return 1;   /* "matrix" */
    if (path[0] == '\0')
        return 2;   /* "none" */

    /* A browsed image: highlight the row it actually is, so reopening the
     * panel lands on the wallpaper you are looking at rather than on
     * "Synapse". */
    for (int i = 0; i < s->wppick.found_count; i++)
        if (strcmp(s->wppick.found[i], path) == 0)
            return wppick_option_count + i;

    return 0;       /* the bundled image (or a path from synuirc) */
}

/* Keep the highlight inside the visible window of rows. */
static void wppick_scroll_to_selection(syn_server_t *s)
{
    if (s->wppick.selected < s->wppick.scroll)
        s->wppick.scroll = s->wppick.selected;
    if (s->wppick.selected >= s->wppick.scroll + WPPICK_ROWS)
        s->wppick.scroll = s->wppick.selected - WPPICK_ROWS + 1;
    if (s->wppick.scroll < 0) s->wppick.scroll = 0;
}

/* Apply an option token to the live config and repaint. Mirrors the synuirc
 * `wallpaper` key semantics for the built-in keywords. */
static void wppick_apply(syn_server_t *s, int idx, bool commit)
{
    if (idx < 0 || idx >= wppick_total(s)) return;

    /* ⛔ An action row changes no wallpaper, ever — and on the preview pass it
     * does nothing whatsoever. See wppick_is_action(): this panel applies as
     * the highlight moves, so without this, scrolling past the row would open
     * the browser. On Enter it opens it and gets out of the way, because the
     * browser wants the keyboard and two full-screen surfaces both asking for
     * it is a panel nobody can drive.
     *
     * The header's [w] button is the same door, one keypress away instead of
     * one scroll and an Enter — both end up in wppick_wallhaven_open(). */
    if (wppick_is_action(s, idx)) {
        if (!commit) return;
        /* ⚠ NOT hidden here — see wppick_wallhaven_open(). */
        wppick_wallhaven_open();
        return;
    }

    const char *scope = wppick_scope_output(s);
    syn_wallpaper_src_t was;
    wppick_scope_state(s, &was, NULL);

    /* A Workshop wallpaper is not painted here at all: hand the id to
     * synui-wpengine, which starts linux-wallpaperengine as a layer-shell
     * client above wallpaper_tree. It persists the choice itself (per output,
     * which is why the scope can just be passed along), so there is nothing
     * for wallpaper_state_save to record. */
    int we = wppick_we_index(s, idx);
    if (we >= 0) {
        /* An asset pack, or a preset whose base wallpaper is not subscribed
         * (see wp_project_meta): the engine would stop, come back up with
         * nothing to draw, and leave the screen on whatever synui paints
         * underneath. Keeping what is already there is both the honest outcome
         * and the cheaper one; the row's subtitle is what explains it. */
        if (!s->wppick.we[we].renderable) {
            wlr_log(WLR_INFO, "synui: wppick: '%s' (%s) is not a renderable "
                    "wallpaper — leaving the current one alone",
                    s->wppick.we[we].title, s->wppick.we[we].type);
            return;
        }

        char cmd[160];
        snprintf(cmd, sizeof(cmd), "synui-wpengine set %s %s",
                 s->wppick.we[we].id, scope ? scope : "all");
        synui_spawn(cmd);
        if (scope) {
            syn_wp_output_t *e = wallpaper_output_entry(&s->config, scope, true);
            if (e) e->src = SYN_WP_SRC_WPENGINE;
        } else {
            s->config.wallpaper_src = SYN_WP_SRC_WPENGINE;
        }
        return;
    }

    /* Leaving a Workshop wallpaper: the engine holds an opaque surface over
     * the output, so repainting wallpaper.c under it would change nothing
     * visible until it is gone. Only this scope's surface goes — another
     * monitor's Workshop wallpaper is not ours to stop.
     *
     * Whether one is up has to come from the engine's own state file, NOT from
     * wallpaper_src. SYN_WP_SRC_WPENGINE is set when a pick is made and is
     * never persisted (the script owns that), while synuirc autostarts
     * `synui-wpengine restore` at login — so after every restart the config
     * says IMAGE with the engine painting over it, this stop was skipped, and
     * picking anything did nothing visible at all. current_index() already
     * reads the state file for exactly this reason; this is the other half. */
    char live_we[24];
    wppick_active_we(live_we, sizeof(live_we), scope);

    if (live_we[0] || was == SYN_WP_SRC_WPENGINE) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "synui-wpengine off%s%s",
                 scope ? " " : "", scope ? scope : "");
        synui_spawn(cmd);
    }

    /* "All monitors" means exactly that: the per-monitor overrides go, so a
     * screen that had its own picture follows the global choice again rather
     * than ignoring the pick that was just made. */
    if (!scope) wallpaper_output_clear(&s->config, NULL);

    /* A row past the built-ins is an image the scan found: point the config
     * straight at its path. wallpaper_state_save already persists an arbitrary
     * path, so a browsed choice survives a restart with no extra machinery. */
    const char *tok = idx >= wppick_option_count
                      ? s->wppick.found[idx - wppick_option_count]
                      : wppick_options[idx].token;

    if (scope) {
        wallpaper_output_apply(&s->config, scope, tok, -1);
    } else if (strcmp(tok, "matrix") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_MATRIX;
    } else if (strcmp(tok, "default") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        strncpy(s->config.wallpaper, SYNUI_DATADIR "/wallpaper.png",
                sizeof(s->config.wallpaper) - 1);
        s->config.wallpaper[sizeof(s->config.wallpaper) - 1] = '\0';
    } else if (strcmp(tok, "none") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        s->config.wallpaper[0] = '\0';
    } else {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        snprintf(s->config.wallpaper, sizeof(s->config.wallpaper), "%s", tok);
    }

    /* Repaint the static backend (decodes/clears wallpaper_buf). The matrix
     * backend picks up / tears down on the next frame via matrix_active(). */
    wallpaper_reload(s);

    /* Kick a frame on every output so the change is visible at once: the
     * matrix path renders its first frame (and self-sustains), and a switch
     * away from matrix runs matrix_output_frame() once to drop its buffer. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);

    /* Only a committed pick is written out. A preview that is arrowed past —
     * or abandoned with Esc — used to persist here on every keypress, so the
     * last row the highlight happened to cross became the saved wallpaper. */
    if (commit) wallpaper_state_save(s);
}

/*
 * Set the wallpaper to an explicit path, without opening the picker.
 *
 * What `synctl dispatch wallpaper /path/to.jpg` runs, and the reason the
 * `wallpaper` action now looks at its argument at all. The caller that needs it
 * is the Antiquity shell's theme picker: each of its palettes names the
 * wallpaper it was drawn against, and a palette that cannot actually put that
 * wallpaper on screen is only half a theme — its taskbar is glassTintColor at
 * 20% over whatever is behind it, so the wallpaper IS most of the bar's colour
 * and the two cannot be chosen independently.
 *
 * Deliberately global-scope only. The picker can target one monitor; a theme
 * cannot sensibly say "and only on DP-1", and inventing a scope argument here
 * would be a second, half-implemented copy of a UI that already exists.
 *
 * Everything below is wppick_apply's own committed-pick path for a browsed
 * image, in the same order and for the same reasons — the engine has to be
 * stopped before anything is painted under it, and the per-output overrides
 * have to go or a monitor with its own picture ignores the change.
 */
void wppick_set_path(syn_server_t *s, const char *path)
{
    if (!path || !*path) return;

    /* Refuse a path that is not there, rather than "setting" it. wallpaper.c
     * treats an undecodable image as no wallpaper and paints bg_color, so
     * without this check a theme picker on a box where the wallpapers were
     * never installed would answer "apply theme" with a blank desktop — and
     * persist that, since wallpaper.state overrides synuirc. */
    if (access(path, R_OK) != 0) {
        wlr_log(WLR_ERROR, "synui: wallpaper '%s': %s — leaving the current "
                "one alone", path, strerror(errno));
        return;
    }

    syn_wallpaper_src_t was = s->config.wallpaper_src;
    char live_we[24];
    wppick_active_we(live_we, sizeof(live_we), NULL);
    if (live_we[0] || was == SYN_WP_SRC_WPENGINE)
        synui_spawn("synui-wpengine off");

    wallpaper_output_clear(&s->config, NULL);

    s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
    snprintf(s->config.wallpaper, sizeof(s->config.wallpaper), "%s", path);

    wallpaper_reload(s);

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);

    /* Persisted, unlike a preview: this arrives only from a deliberate act
     * (picking a theme), so it is exactly the "most recent explicit intent"
     * that wallpaper.state is for. */
    wallpaper_state_save(s);

    wlr_log(WLR_INFO, "synui: wallpaper set to '%s'", path);
}

/* Does the current scope have a Workshop wallpaper on screen right now?
 *
 * Same two-part test wppick_apply uses to decide whether to stop the engine:
 * the engine's own state file is the truth (a `synui-wpengine off` from a
 * terminal would make a mirrored copy stale), with wallpaper_src covering a
 * pick made in this session that the script has not recorded yet. */
static bool wppick_scope_has_we(syn_server_t *s)
{
    const char *scope = wppick_scope_output(s);

    char live_we[24];
    wppick_active_we(live_we, sizeof(live_we), scope);
    if (live_we[0]) return true;

    syn_wallpaper_src_t src;
    wppick_scope_state(s, &src, NULL);
    return src == SYN_WP_SRC_WPENGINE;
}

/* Put back the wallpaper config the panel opened with, and repaint. Nothing
 * was persisted while previewing, so the on-disk state already agrees with
 * this and must not be rewritten. */
static void wppick_restore(syn_server_t *s)
{
    snprintf(s->config.wallpaper, sizeof(s->config.wallpaper), "%s",
             s->wppick.saved.wallpaper);
    s->config.wallpaper_mode = s->wppick.saved.mode;
    s->config.wallpaper_src  = s->wppick.saved.src;
    memcpy(s->config.wallpaper_out, s->wppick.saved.out,
           sizeof(s->config.wallpaper_out));
    s->config.wallpaper_out_n = s->wppick.saved.out_n;

    wallpaper_reload(s);
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);
}

/* Highlight moved. Images and built-ins are cheap enough to apply live, which
 * is the whole point of the panel; a Workshop wallpaper spawns a GPU process
 * and would thrash if it fired on every arrow key, so it is only remembered
 * here and committed on Enter.
 *
 * A Workshop wallpaper already on screen makes every row defer, not just the
 * Workshop ones. Two reasons, and the first is the one that matters:
 *
 *   - Applying a static wallpaper stops the engine, and linux-wallpaperengine
 *     cannot rebuild a layer surface it has lost, so "undo" would mean
 *     restarting the process — seconds of the wallpaper vanishing and coming
 *     back for every arrow key. Not stopping it in the first place is both
 *     cheaper and what the user asked for by not pressing Enter yet.
 *   - There is nothing to see anyway: the engine holds an OPAQUE surface over
 *     the output, so a static wallpaper painted underneath is invisible until
 *     the engine is gone. The live preview was buying nothing here and paying
 *     for it with the only destructive act in the panel. */
static void wppick_preview(syn_server_t *s, int idx)
{
    /* ⚠ AN ACTION ROW DEFERS THE SAME WAY A WORKSHOP ROW DOES, and that is why
     * it uses the same field rather than a second one. `pending_we` holds a ROW
     * INDEX and means "this row is waiting for Enter" — which is precisely what
     * an action row needs, and Enter's existing commit path then reaches
     * wppick_apply() with commit set.
     *
     * ⛔ WITHOUT THIS THE ROW IS DEAD. Enter only commits a DEFERRED row; for
     * everything else it persists whatever the highlight already applied and
     * closes. An action row applies nothing on highlight, so it was never
     * deferred, so Enter had nothing to commit and the row did nothing at all —
     * which the rig caught, and which reading the apply path alone would not
     * have shown. */
    if (wppick_is_action(s, idx) ||
        wppick_we_index(s, idx) >= 0 || wppick_scope_has_we(s)) {
        s->wppick.pending_we = idx;
        return;
    }

    s->wppick.pending_we = -1;
    wppick_apply(s, idx, false);
    s->wppick.previewed = true;
}

/* ── Browse: find images on disk ─────────────────────────── */

/* wallpaper.c decodes PNG and JPEG; anything else would just fail to load. */
static bool wp_is_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".png")  == 0 ||
           strcasecmp(dot, ".jpg")  == 0 ||
           strcasecmp(dot, ".jpeg") == 0;
}

static int wp_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void wppick_scan_dir(char (*out)[256], int *count, int max,
                            const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && *count < max) {
        if (e->d_name[0] == '.') continue;
        if (!wp_is_image(e->d_name)) continue;

        char path[256];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path))
            continue;   /* path too long to store — skip rather than truncate */

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        /* The bundled image is already offered as "Synapse"; listing it again
         * under its filename would just be a duplicate row. */
        if (strcmp(path, SYNUI_DATADIR "/wallpaper.png") == 0) continue;

        for (int i = 0; i < *count; i++)
            if (strcmp(out[i], path) == 0) goto next;

        snprintf(out[(*count)++], sizeof(out[0]), "%s", path);
    next:
        ;
    }
    closedir(d);
}

/* Where people actually keep wallpapers. Scanned in order, deduped by path.
 * Writes into the caller's array so the saver panel's "Lock image" row can
 * offer exactly the same pictures without borrowing (and clobbering) the
 * picker's own list, which stays live for as long as its panel is open. */
int wppick_scan_into(char (*out)[256], int max)
{
    int count = 0;

    const char *home = getenv("HOME");
    if (home && *home) {
        static const char *rel[] = {
            "/Pictures/Wallpapers",
            "/Pictures/wallpapers",
            "/Pictures",
            "/.local/share/wallpapers",
            "/Wallpapers",
        };
        for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
            char dir[256];
            if (snprintf(dir, sizeof(dir), "%s%s", home, rel[i]) < (int)sizeof(dir))
                wppick_scan_dir(out, &count, max, dir);
        }
    }

    wppick_scan_dir(out, &count, max, "/usr/share/backgrounds");
    wppick_scan_dir(out, &count, max, "/usr/share/wallpapers");

    /* Stable, predictable order — readdir's is neither, and a list that
     * reshuffles between openings is miserable to use. */
    qsort(out, (size_t)count, sizeof(out[0]), wp_cmp);

    wlr_log(WLR_INFO, "synui: wppick: %d image(s) found", count);
    return count;
}

void wppick_scan(syn_server_t *s)
{
    s->wppick.found_count = wppick_scan_into(s->wppick.found, WPPICK_FOUND_MAX);
}

/* ── Browse: Steam Workshop (Wallpaper Engine) ───────────── */

/* Read one JSON string token, the opening quote already consumed. Titles carry
 * escapes and plenty of non-ASCII; \" and \\ pass through as the literal
 * character and other escapes are dropped rather than emitting a stray
 * backslash. Everything else (UTF-8 included) is byte-copied. Always drains
 * the whole token even when it does not fit, so the caller stays in sync. */
static void wp_json_token(FILE *f, char *out, size_t n)
{
    size_t i = 0;
    int c;
    while ((c = fgetc(f)) != EOF && c != '"') {
        if (c == '\\') {
            c = fgetc(f);
            if (c == EOF) break;
            if (c != '"' && c != '\\') continue;
        }
        if (out && i + 1 < n) out[i++] = (char)c;
    }
    if (out && n) out[i] = '\0';
}

/* Pull the TOP-LEVEL "title" and "type" out of a project.json. Returns false
 * when the item is positively identified as something the engine cannot render.
 *
 * No JSON library here, but a flat strstr for "type" is not good enough: a
 * project.json carries a general.properties object whose every entry has its
 * own "type" ("color", "bool", …) and often "text", and those come BEFORE the
 * top-level keys in the files Wallpaper Engine writes. Matching the first hit
 * labelled scene wallpapers "color" and left titles as bare ids. So track
 * brace/bracket depth and only accept keys sitting directly in the root
 * object. Streamed rather than slurped because the properties block can be
 * far larger than the two strings we actually want. */
/* Is there a subscription with this id in the Workshop tree? Used to tell a
 * preset whose base wallpaper is installed from one whose is not. */
static bool wp_id_present(const char *root, const char *id)
{
    char p[512];
    if (snprintf(p, sizeof(p), "%s/%s", root, id) >= (int)sizeof(p))
        return false;

    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool wp_project_meta(const char *path, const char *root,
                            char *title, size_t tn,
                            char *type, size_t yn, char *preview, size_t pn)
{
    title[0] = '\0';
    type[0]  = '\0';
    preview[0] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) return true;   /* unreadable — let the engine be the judge */

    int depth = 0;
    char pending[32] = "";   /* the depth-1 key whose value comes next */
    char category[32] = "";  /* "Asset" on an editor asset pack */
    char dep[24] = "";       /* the wallpaper a preset re-configures */
    bool preset = false;     /* a saved property set for some other wallpaper */
    int c;

    while ((c = fgetc(f)) != EOF) {
        if (c == '{' || c == '[') { depth++; pending[0] = '\0'; continue; }
        if (c == '}' || c == ']') { depth--; pending[0] = '\0'; continue; }
        if (c != '"') continue;

        /* A string. Whether it is a key or a value is decided by the next
         * non-space character: ':' means key. */
        char tok[256];
        wp_json_token(f, tok, sizeof(tok));

        int nxt;
        while ((nxt = fgetc(f)) != EOF && (nxt == ' ' || nxt == '\t' ||
                                           nxt == '\n' || nxt == '\r'))
            ;

        if (nxt == ':') {
            /* Only root-object keys are the ones we mean. */
            if (depth == 1) {
                snprintf(pending, sizeof(pending), "%s", tok);
                /* "preset" is an OBJECT, so the '{' that follows bumps the
                 * depth and clears pending before the value branch below could
                 * ever see it. Note it here, while the key is still in hand. */
                if (strcmp(tok, "preset") == 0) preset = true;
            } else {
                pending[0] = '\0';
            }
            continue;
        }
        if (nxt != EOF) ungetc(nxt, f);

        if (depth != 1 || !pending[0]) continue;

        if (strcmp(pending, "title") == 0)
            snprintf(title, tn, "%s", tok);
        else if (strcmp(pending, "type") == 0)
            snprintf(type, yn, "%s", tok);
        else if (strcmp(pending, "category") == 0)
            snprintf(category, sizeof(category), "%s", tok);
        else if (strcmp(pending, "dependency") == 0)
            snprintf(dep, sizeof(dep), "%s", tok);
        else if (strcmp(pending, "preview") == 0)
            snprintf(preview, pn, "%s", tok);
        pending[0] = '\0';

        /* All three, not just title+type: every one of the 139 subscriptions
         * here names a preview, and stopping early at the first two would skip
         * it whenever it is spelled last in the file. */
        if (title[0] && type[0] && preview[0]) break;
    }
    fclose(f);

    if (type[0]) return true;

    /* No top-level "type". Two kinds of subscription land in the Workshop tree
     * without one, and only one of them can be put on screen.
     *
     * A PRESET is a saved property set plus a "dependency" naming the wallpaper
     * it re-configures — in practice most of them are configurations of audio
     * visualisers, which is why they look like wallpapers in the Workshop. Its
     * own id draws nothing ("Project type missing"), but synui-wpengine resolves
     * it to that base wallpaper plus a --set-property per saved value, which is
     * what Wallpaper Engine itself does with one. So a preset IS renderable —
     * as long as the wallpaper it depends on is subscribed too. When it is not
     * there is nothing to configure, and the row says so rather than starting
     * an engine that would come up blank.
     *
     * An ASSET pack ("category": "Asset", "file": "assets.json") is a
     * particle/visualiser component for the Wallpaper Engine editor. There is
     * no wallpaper inside it at all, so it stays a labelled dead row and
     * wppick_apply leaves the current wallpaper alone.
     *
     * Anything else typeless is left renderable on purpose: a subscription this
     * parser simply did not understand is the engine's call, not ours. */
    if (preset) {
        /* `dep` is reliably in hand here whatever order the file spells its
         * keys in: the early break above needs a "type", and a preset never
         * has one, so the scan always ran to EOF. */
        if (dep[0] && wp_id_present(root, dep)) {
            snprintf(type, yn, "%s", _("preset"));
            return true;
        }
        snprintf(type, yn, "%s", _("preset (base missing)"));
        return false;
    }
    if (strcasecmp(category, "asset") == 0) {
        snprintf(type, yn, "%s", _("asset (not a wallpaper)"));
        return false;
    }
    return true;
}

/* Is `id` already listed? Roots are walked most-specific first, so an id that
 * is taken came from a root that outranks the one being scanned now. */
static bool wppick_we_has_id(syn_server_t *s, const char *id)
{
    for (int i = 0; i < s->wppick.we_count; i++)
        if (strcmp(s->wppick.we[i].id, id) == 0) return true;
    return false;
}

/* One Wallpaper Engine content root: a directory of numbered wallpaper dirs. */
static void wppick_scan_we_root(syn_server_t *s, const char *root)
{
    DIR *d = opendir(root);
    if (!d) return;   /* No such root — no rows, no error */

    struct dirent *e;
    while ((e = readdir(d)) && s->wppick.we_count < WPPICK_WE_MAX) {
        if (e->d_name[0] == '.') continue;
        if (wppick_we_has_id(s, e->d_name)) continue;

        char proj[512];
        if (snprintf(proj, sizeof(proj), "%s/%s/project.json", root, e->d_name)
                >= (int)sizeof(proj))
            continue;

        /* Read the title at full length and let syn_utf8_copy do the cutting
         * below. A plain snprintf into the row's buffer cuts on a byte, and a
         * title long enough to need cutting is long enough to be non-ASCII —
         * the half character it leaves behind is invalid UTF-8, which cairo
         * refuses to draw, taking every row under it blank with it. */
        char title[256], type[32], preview[96];
        bool renderable = wp_project_meta(proj, root, title, sizeof(title),
                                          type, sizeof(type),
                                          preview, sizeof(preview));

        if (!type[0]) snprintf(type, sizeof(type), "?");

        /* project.json spells the type both "Web" and "web" depending on who
         * published it; fold it so the subtitle column does not look ragged. */
        for (char *c = type; *c; c++)
            if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';

        int i = s->wppick.we_count++;
        snprintf(s->wppick.we[i].id,    sizeof(s->wppick.we[i].id),    "%s", e->d_name);
        syn_utf8_copy(s->wppick.we[i].title, sizeof(s->wppick.we[i].title), title);
        snprintf(s->wppick.we[i].type,  sizeof(s->wppick.we[i].type),  "%s", type);
        s->wppick.we[i].renderable = renderable;

        /* Full path now, while the root and the id are both in hand. project.json
         * names the file (always "preview.<ext>" in practice, but it is a field,
         * not a convention, so it is read rather than assumed). */
        s->wppick.we[i].preview[0] = '\0';
        if (preview[0] && strchr(preview, '/') == NULL) {
            /* A truncated path would name some other file, or nothing — either
             * way it is not this wallpaper, so drop it and let the pane say
             * there is no preview rather than decode whatever it landed on. */
            int n = snprintf(s->wppick.we[i].preview,
                             sizeof(s->wppick.we[i].preview),
                             "%s/%s/%s", root, e->d_name, preview);
            if (n < 0 || n >= (int)sizeof(s->wppick.we[i].preview))
                s->wppick.we[i].preview[0] = '\0';
        }

        /* A stale or partial subscription (no project.json, or no title in it)
         * still gets a row — the id is enough to hand to the engine. Checked
         * after the copy, since a title that was nothing but invalid bytes
         * comes out of it empty too. */
        if (!s->wppick.we[i].title[0])
            snprintf(s->wppick.we[i].title, sizeof(s->wppick.we[i].title),
                     "%s", e->d_name);
    }
    closedir(d);
}

/*
 * Every Wallpaper Engine wallpaper this machine has, from every root.
 *
 * Wallpaper Engine is Steam AppID 431960 and subscriptions land in the usual
 * Workshop content tree — but that is not the only place a wallpaper lives any
 * more. The `synapse-wallpapers` package installs ours into a SYSTEM tree, and
 * a video wallpaper needs no Steam and no assets directory to play, so an ISO
 * with no Steam on it still ships four working wallpapers.
 *
 * Scanning only the Workshop tree is what made them invisible: `opendir` on a
 * machine with no Steam fails, this returned, and the picker showed no
 * Wallpaper Engine rows at all — silently, since "no Workshop tree" is the
 * normal state of a box without Wallpaper Engine and never was an error. The
 * wallpapers were installed, the engine would have played them, and there was
 * no way to choose one. synui-wpengine's wp_dir() learned about the system
 * tree when the package landed; this did not.
 *
 * SAME ROOTS, SAME ORDER as that function, because the two have to agree about
 * what an id means: the picker hands the engine a BARE id (see the `set` in
 * wppick_apply), and the engine resolves it against these roots in this order.
 * A user's own subscription therefore shadows a system wallpaper of the same
 * id in both places rather than in only one of them.
 */
static void wppick_scan_workshop(syn_server_t *s)
{
    s->wppick.we_count = 0;

    const char *home = getenv("HOME");
    char workshop[256];
    if (home && *home &&
        snprintf(workshop, sizeof(workshop),
                 "%s/.local/share/Steam/steamapps/workshop/content/431960",
                 home) < (int)sizeof(workshop))
        wppick_scan_we_root(s, workshop);

    /* Overridable by the same variable synui-wpengine honours, so a test rig
     * can point both halves at one scratch tree. */
    const char *sysroot = getenv("SYNUI_WPENGINE_SYSROOT");
    wppick_scan_we_root(s, (sysroot && *sysroot) ? sysroot : WPPICK_WE_SYSROOT);

    wlr_log(WLR_INFO, "synui: wppick: %d Wallpaper Engine wallpaper(s) found",
            s->wppick.we_count);
}

/* The image that represents a row, for the panel's preview pane.
 *
 * NULL where there is genuinely nothing to show rather than a placeholder
 * picture: "None" is a solid colour and Matrix is a live GPU shader, so any
 * still image for either would be inventing one. render.c draws its own
 * caption in that case, which is honest about there being no preview instead
 * of implying the decode failed.
 */
const char *wppick_row_preview(syn_server_t *s, int row)
{
    if (row < 0 || row >= wppick_total(s)) return NULL;

    int we = wppick_we_index(s, row);
    if (we >= 0)
        return s->wppick.we[we].preview[0] ? s->wppick.we[we].preview : NULL;

    /* A browsed image is its own preview. */
    if (row >= wppick_option_count)
        return s->wppick.found[row - wppick_option_count];

    if (strcmp(wppick_options[row].token, "default") == 0)
        return SYNUI_DATADIR "/wallpaper.png";

    return NULL;   /* matrix, none */
}

/* Built-ins first, then the images the scan turned up, then Workshop. */
int wppick_total(syn_server_t *s)
{
    return wppick_option_count + s->wppick.found_count + s->wppick.we_count;
}

/* Row index → Workshop entry, or -1 when the row is a built-in or an image. */
static int wppick_we_index(syn_server_t *s, int row)
{
    int base = wppick_option_count + s->wppick.found_count;
    if (row < base || row >= base + s->wppick.we_count) return -1;
    return row - base;
}

/* One row's text. render.c draws; the labels live here so the built-in and
 * found rows cannot drift apart. */
void wppick_row(syn_server_t *s, int row, const char **label, const char **desc)
{
    if (row < wppick_option_count) {
        *label = wppick_options[row].label;
        *desc  = wppick_options[row].desc;
        return;
    }

    int we = wppick_we_index(s, row);
    if (we >= 0) {
        /* The Workshop title is the only human-readable handle these have —
         * the id is a bare number. Type goes in the subtitle so the animated
         * ones are obvious before you commit to starting the engine. */
        *label = s->wppick.we[we].title;
        *desc  = s->wppick.we[we].type;
        return;
    }

    const char *path = s->wppick.found[row - wppick_option_count];

    /* Show the filename, with the directory as the subtitle: the basename is
     * what identifies the image, and a full path would not fit the column. */
    const char *slash = strrchr(path, '/');
    *label = slash ? slash + 1 : path;
    *desc  = path;
}

void wppick_show(syn_server_t *s)
{
    wppick_scan(s);
    wppick_scan_workshop(s);
    wppick_scan_outputs(s);
    s->wppick.visible = 1;
    /* Opens on "all monitors" every time. A scope that persisted would be an
     * invisible mode: the panel looks the same and the next pick silently
     * lands on one screen. */
    s->wppick.scope = -1;
    s->wppick.selected = current_index(s);
    s->wppick.scroll = 0;
    s->wppick.pending_we = -1;

    /* Snapshot before anything can preview over it. */
    s->wppick.previewed = false;
    snprintf(s->wppick.saved.wallpaper, sizeof(s->wppick.saved.wallpaper),
             "%s", s->config.wallpaper);
    s->wppick.saved.mode  = s->config.wallpaper_mode;
    s->wppick.saved.src   = s->config.wallpaper_src;
    memcpy(s->wppick.saved.out, s->config.wallpaper_out,
           sizeof(s->wppick.saved.out));
    s->wppick.saved.out_n = s->config.wallpaper_out_n;

    wppick_scroll_to_selection(s);
    synui_render_wppick(s);
}

void wppick_hide(syn_server_t *s)
{
    /* Every close that is not Enter lands here with `previewed` still set —
     * Esc, and Super+W toggled shut. Reverting here rather than in the Esc
     * case is what stops a second close path from quietly keeping a preview.
     * Enter clears the flag itself once it has committed. */
    if (s->wppick.previewed) {
        wppick_restore(s);
        s->wppick.previewed = false;
    }
    s->wppick.pending_we = -1;
    s->wppick.visible = 0;
    synui_render_wppick(s);
    ctlpanel_child_closed(s, "wallpaper");
}

void wppick_toggle(syn_server_t *s)
{
    if (s->wppick.visible) wppick_hide(s);
    else                   wppick_show(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. This panel is the one place the
 * contract needs a second gesture, because it has two distinct steps that the
 * keyboard splits across two keys: arrowing PREVIEWS, Enter COMMITS. That split
 * is load-bearing here — a Workshop pick restarts a GPU process, and Esc has to
 * be able to put back what you found — so a single click cannot mean both.
 *
 * So: one click previews (the arrow keys' job), a double click commits (Enter's
 * job). 400ms, the same window the titlebar and the desktop icons already use.
 * A click off the panel is Esc, which reverts the preview like every other exit
 * that is not Enter.
 */

int wppick_motion(syn_server_t *s, double lx, double ly)
{
    (void)lx; (void)ly;
    /* Deliberately no hover preview. Moving the selection here applies the
     * wallpaper for real (that is what "preview" means in this panel — the
     * desktop actually changes), and doing that to every row the pointer
     * crosses on its way down a list of 131 Workshop entries would restart a
     * GPU process per row. The click is the deliberate act; hovering is not. */
    return s->wppick.visible ? 1 : 0;
}

int wppick_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec)
{
    if (!s->wppick.visible) return 0;

    if (!hit_in_panel(&s->wppick.hit, lx, ly)) {
        /* Esc, not Enter: abandon the deferred row rather than commit it. */
        s->wppick.pending_we = -1;
        wppick_hide(s);
        return 1;
    }

    if (button != BTN_LEFT) return 1;

    /* The header's [w] Wallhaven button. A BUTTON, so one click acts — the
     * double-click rule below is about picking a wallpaper, and this picks
     * none. render.c records the rect where it draws the label; spot 0 is the
     * only one this panel has. */
    if (hit_spot_at(&s->wppick.hit, lx, ly) == 0) {
        s->wppick.pending_we = -1;
        wppick_wallhaven_open();
        wppick_hide(s);
        return 1;
    }

    int i = hit_index_at(&s->wppick.hit, lx, ly);
    if (i < 0 || i >= wppick_total(s)) return 1;   /* chrome / preview pane */

    bool dbl = (s->wppick.last_click_row == i) &&
               (time_msec - s->wppick.last_click_ms < 400);
    s->wppick.last_click_row = dbl ? -1 : i;
    s->wppick.last_click_ms  = time_msec;

    if (!dbl) {
        /* First click: select and preview — exactly what Up/Down do. */
        s->wppick.selected = i;
        wppick_preview(s, i);
        wppick_scroll_to_selection(s);
        synui_render_wppick(s);
        return 1;
    }

    /* Second click: Enter. Same two cases it has — a deferred row to apply, or
     * a live preview that only needs writing down. */
    if (s->wppick.pending_we >= 0) {
        wppick_apply(s, s->wppick.pending_we, true);
        s->wppick.pending_we = -1;
        s->wppick.previewed = false;
    } else if (s->wppick.previewed) {
        wallpaper_state_save(s);
        s->wppick.previewed = false;
    }
    wppick_hide(s);
    return 1;
}

int wppick_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->wppick.visible) return 0;
    if (delta == 0) return 1;

    /* The wheel scrolls the WINDOW and leaves the selection alone, unlike every
     * other panel here. Moving the selection is what previews, and a wheel that
     * applied a wallpaper per notch would be unusable. */
    int total = wppick_total(s);
    if (total <= WPPICK_ROWS) return 1;

    s->wppick.scroll += delta > 0 ? 3 : -3;
    if (s->wppick.scroll > total - WPPICK_ROWS) s->wppick.scroll = total - WPPICK_ROWS;
    if (s->wppick.scroll < 0) s->wppick.scroll = 0;
    synui_render_wppick(s);
    return 1;
}

int wppick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->wppick.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        /* Enter is the only thing that writes a choice down. Either a deferred
         * row has to be applied now, or a live preview is already on screen
         * and just needs persisting — it was deliberately not saved while the
         * highlight was moving. */
        if (s->wppick.pending_we >= 0) {
            wppick_apply(s, s->wppick.pending_we, true);
            s->wppick.pending_we = -1;
            s->wppick.previewed = false;
        } else if (s->wppick.previewed) {
            wallpaper_state_save(s);
            s->wppick.previewed = false;
        }
        wppick_hide(s);
        return 1;
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        /* Esc abandons a deferred row rather than committing it — arrowing
         * through 131 Workshop entries must not leave one applied. A live
         * preview is reverted by wppick_hide for the same reason. */
        s->wppick.pending_we = -1;
        wppick_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->wppick.selected > 0) {
            s->wppick.selected--;
            wppick_preview(s, s->wppick.selected);
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->wppick.selected < wppick_total(s) - 1) {
            s->wppick.selected++;
            wppick_preview(s, s->wppick.selected);
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_Tab:
        /* Cycle the scope: all monitors → each connector → back. Nothing is
         * applied by moving it — it only redirects the next pick — so this is
         * safe to spin through, and the highlight follows to whatever that
         * scope is currently showing. */
        if (s->wppick.out_count > 0) {
            s->wppick.scope++;
            if (s->wppick.scope >= s->wppick.out_count) s->wppick.scope = -1;
            s->wppick.pending_we = -1;
            s->wppick.selected = current_index(s);
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_m: {
        /* Cycle fill → fit → stretch → center → tile. The mode was previously
         * only reachable by hand-editing synuirc's wallpaper_mode, which is why
         * nobody knew stretch and center already existed.
         *
         * Follows the scope like a pick does: under "all monitors" it moves the
         * global mode (and the overrides are gone anyway once a pick is made
         * there), under a connector only that monitor's. */
        const char *scope = wppick_scope_output(s);
        syn_wallpaper_mode_t cur;
        wallpaper_effective(&s->config, scope, NULL, NULL, &cur);
        int next = (cur + 1) % SYN_WALLPAPER_MODE_COUNT;

        if (scope) wallpaper_output_apply(&s->config, scope, NULL, next);
        else       s->config.wallpaper_mode = (syn_wallpaper_mode_t)next;

        /* Repaint every output rather than just the selected entry: even a
         * scoped change has to go through the same reload, and the others
         * simply repaint identically. */
        wallpaper_reload(s);
        syn_output_t *wo;
        wl_list_for_each(wo, &s->outputs, link)
            wlr_output_schedule_frame(wo->wlr_output);
        wallpaper_state_save(s);

        /* Unlike a highlight, pressing `m` is someone deliberately changing
         * the mode, so it commits — and the snapshot has to move with it or
         * closing with Esc would take the new mode back out again. */
        s->wppick.saved.mode  = s->config.wallpaper_mode;
        memcpy(s->wppick.saved.out, s->config.wallpaper_out,
               sizeof(s->wppick.saved.out));
        s->wppick.saved.out_n = s->config.wallpaper_out_n;

        synui_render_wppick(s);
        return 1;
    }
    case XKB_KEY_w:
        /* Where more wallpapers come from. The header button's key, and the
         * counterpart of the browser's own `w`, which comes back here — so w
         * flips between the two halves of picking a wallpaper.
         *
         * ⛔ CLOSES THE PICKER. Unlike the Wallhaven row this commits nothing,
         * so a deferred row is abandoned rather than applied, exactly as Esc
         * abandons it: pressing w is going somewhere else, not choosing. */
        s->wppick.pending_we = -1;
        wppick_wallhaven_open();
        wppick_hide(s);
        return 1;
    case XKB_KEY_r:
        /* Rescan without closing — for when you have just saved an image into
         * ~/Pictures and want it in the list. */
        wppick_scan(s);
        if (s->wppick.selected >= wppick_total(s))
            s->wppick.selected = wppick_total(s) - 1;
        wppick_scroll_to_selection(s);
        synui_render_wppick(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
