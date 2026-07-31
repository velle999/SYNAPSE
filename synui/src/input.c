/*
 * input.c — Keyboard, pointer, touch and tablet handling
 *
 * Keybindings are table-driven (config.c seeds the defaults; `bind =` lines
 * in synuirc add or override).
 *
 * The default table is NOT reproduced here. It used to be, and it drifted —
 * it still listed Super+T as the task manager long after that moved, and never
 * gained Super+B/I/V/G at all, so the file that handles keys documented a
 * keymap the compositor did not have. There are exactly two hand-written
 * copies now, and both are somewhere a reader would look for the real thing:
 *
 *   - seed_default_binds() in config.c — the authoritative table.
 *   - `synui --help` — the same list for users, marked to be kept in step.
 *
 * The control panel's shortcuts column (Super+C) is generated from the live
 * bind table at runtime, so it is always right, and it is the fastest way to
 * see what a running synui is actually bound to.
 *
 * A couple of behaviours worth knowing that the table cannot express:
 *   Alt+Tab              MRU order — the last-used window, not the
 *                        stacking-order walk Super+J/K do. Hold Alt and keep
 *                        tapping Tab to go further back (Alt+Shift+Tab
 *                        forward); the switch commits on Alt release.
 *   Super (tapped alone) Start menu — a tap, not a combo, so it is handled in
 *                        keyboard_handle_modifiers rather than the bind table.
 *
 * Pointer (interactive floating window management):
 *   Super + Left-drag    Move the window under the cursor
 *   Super + Right-drag   Resize it from the nearest corner
 *
 * Every relative motion is broadcast to relative-pointer clients and run
 * through the active pointer constraint (constraints.c) — locked pointers
 * swallow the move, confined ones clamp it. Touch is forwarded to the seat
 * with proper per-point focus; tablet tools drive the cursor (pointer
 * emulation: tip = left, stylus buttons = right/middle); touchpad gestures
 * are relayed via pointer-gestures-v1; libinput device options (tap,
 * natural scroll, accel, left-handed) come from synuirc.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-names.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
/* wlr_keyboard_notify_modifiers() — pushing synthetic modifier state (the
 * NumLock lock below) is only declared on the backend-facing interface. */
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>

#include "synui.h"
#include "effects.h"

/* Report user activity to idle-notify clients, and to our own idle stages. */
static inline void notify_activity(syn_server_t *s)
{
    if (s->idle_notifier)
        wlr_idle_notifier_v1_notify_activity(s->idle_notifier, s->seat);
    /* Undoes a dim/blank and rearms the idle stages. Runs before the event is
     * dispatched further, so the click that wakes the screen still lands on
     * whatever is under the cursor rather than on the dim overlay. */
    power_notify_activity(s);
}

/* ── Focus ───────────────────────────────────────────────── */
void focus_view(syn_server_t *s, syn_view_t *view, struct wlr_surface *surface)
{
    if (!s) return;
    if (!view) {
        /* Clear focus entirely — forget the old view too, or keyboard input
         * would keep flowing to a window that may no longer be visible. */
        s->focused_view = NULL;
        wlr_seat_keyboard_notify_clear_focus(s->seat);
        ime_set_focus(s, NULL);      /* no text field is focused any more */
        return;
    }

    syn_view_t *prev = s->focused_view;
    s->focused_view = view;

    /* Stamp the most-recently-used order Alt+Tab walks — but NOT while a cycle
     * is running. Each Tab focuses a view for real (you see it, it takes your
     * keys), and stamping that would make the window you just tabbed to the
     * most recent one, so the next Tab would tab straight back to it and the
     * cycle could never reach a third window. The stamp for the whole cycle
     * lands once, on release, in alttab_finish(). */
    if (!s->alttab.active)
        view->focus_seq = ++s->focus_counter;

    /* L3: brief chromatic-aberration pulse on an actual focus change. */
    if (prev != view)
        effects_notify_focus(s);

    /* Raise to top of scene */
    wlr_scene_node_raise_to_top(view_node(view));

    /* Toggle activated state (X11 clients need this to accept input) and
     * refresh border colours. */
    if (prev && prev != view) {
        view_set_activated(prev, 0);
        view_update_decorations(prev);
        foreign_toplevel_update_state(prev);
    }
    view_set_activated(view, 1);
    view_update_decorations(view);
    foreign_toplevel_update_state(view);

    /* Transparency follows focus: the window that just lost focus drops to
     * inactive_opacity and the new one rises to active_opacity. Decorations were
     * refreshed just above (border rects), but the client surface + titlebar
     * buffer opacity only move when anim_apply_alpha re-pushes them. Cheap no-op
     * while transparency is off (anim_view_opacity returns 1.0). */
    if (s->config.transparency) {
        if (prev && prev != view) anim_apply_alpha(prev);
        anim_apply_alpha(view);
    }

    /* Notify seat */
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(s->seat, surface,
                                        kb->keycodes, kb->num_keycodes,
                                        &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(s->seat, surface, NULL, 0, NULL);

    /* The IME follows keyboard focus: the text field on this surface is now
     * the one fcitx5/ibus composes into. */
    ime_set_focus(s, surface);
}

/* Topmost surface (of any role) under the given layout coordinates. Also
 * returns the owning toplevel view if the surface belongs to one (NULL for
 * layer surfaces, popups, and the compositor's own UI). */
struct wlr_surface *surface_at(syn_server_t *s, double lx, double ly,
                               syn_view_t **view_out, double *sx, double *sy)
{
    if (view_out) *view_out = NULL;

    struct wlr_scene_node *node =
        wlr_scene_node_at(&s->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return NULL;

    struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surf = wlr_scene_surface_try_from_buffer(buf);
    if (!scene_surf) return NULL;

    if (view_out) {
        struct wlr_scene_tree *tree = node->parent;
        while (tree && !tree->node.data)
            tree = tree->node.parent;
        if (tree) *view_out = tree->node.data;
    }
    return scene_surf->surface;
}

syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy)
{
    syn_view_t *view = NULL;
    struct wlr_surface *surf = surface_at(s, lx, ly, &view, sx, sy);
    if (surf && surface) *surface = surf;
    return surf ? view : NULL;
}

/* ── View borders ────────────────────────────────────────── */
/* The chrome itself lives in deco.c; security state just re-tints the border. */
void view_set_security(syn_view_t *view, win_security_t state)
{
    view->security = state;
    view_update_decorations(view);
}

/* ── Keyboard ────────────────────────────────────────────── */
static void alttab_finish(syn_server_t *s);   /* defined with the rest of Alt+Tab */

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_keyboard_t *kb = wl_container_of(listener, kb, modifiers);

    /* Alt let go ends an Alt+Tab cycle and commits the window we landed on.
     *
     * This is the only place that can see it: a modifier release produces a
     * modifiers event, not a key event, so keyboard_handle_key never hears
     * about it. Checked before the IME early-return — a cycle left "active"
     * because fcitx5 happened to be grabbing the keyboard would freeze the MRU
     * order for every later focus change, which is a far stranger bug than any
     * it could save. */
    if (kb->server->alttab.active &&
        !(wlr_keyboard_get_modifiers(kb->wlr_keyboard) & WLR_MODIFIER_ALT))
        alttab_finish(kb->server);

    /* While the IME holds the keyboard, modifiers go to it, not the client. */
    if (ime_handle_modifiers(kb->server, kb->wlr_keyboard))
        return;

    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat,
                                        &kb->wlr_keyboard->modifiers);
}

/* ── Alt+Tab ─────────────────────────────────────────────────
 *
 * Most-recently-used order, which is the whole point: Alt+Tab means "the window
 * I was just in", and holding Alt to press Tab again means "the one before
 * that". The existing focus_next (Super+J/K) walks ws->windows, which is
 * *stacking* order — fine for "next window along", useless for coming back.
 *
 * No snapshot of the candidate list is kept between presses. It is rebuilt from
 * live views every time, so a window that closes mid-cycle simply stops being a
 * candidate — there is no stale pointer to dangle, and no destroy hook to
 * remember to add in the two places (xdg + xwayland) that destroy views. That
 * property is why the target is re-derived in alttab_finish() rather than
 * remembered from the last press: a pending syn_view_t* would need clearing in
 * all four places that null focused_view (two unmaps, two destroys), which is
 * four chances to miss one.
 */
#define ALTTAB_MAX 64

/* Every mapped window, on every virtual desktop, most-recent first — including
 * minimized ones.
 *
 * "Alt+Tab" means "the window I was just in", and that window is just as often
 * on another desktop or sitting minimized as it is on screen. Restricting the
 * cycle to what is currently visible made those windows reachable only if you
 * already remembered where you had put them, which is the thing a switcher
 * exists to save you from.
 *
 * Neither kind can take focus where it stands, so neither is acted on until the
 * cycle commits — see alttab_reveal(). A hidden window keeps its last committed
 * buffer (workspace_switch only disables the scene node), so it still draws a
 * real thumbnail rather than falling back to its app icon. */
static int alttab_candidates(syn_server_t *s, syn_view_t **out, int max)
{
    int n = 0;
    int lo = 0, hi = WORKSPACE_MAX;
    if (!s->config.alt_tab_all_desktops)
        lo = s->active_workspace, hi = lo + 1;

    for (int w = lo; w < hi && n < max; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->mapped) continue;
            if (!v->minimized || s->config.alt_tab_minimized) {
                if (n >= max) break;
                out[n++] = v;
            }
        }
    }

    /* Insertion sort by focus_seq, newest first. n is a handful of windows, and
     * this keeps the "never focused" ones (seq 0) at the back where they
     * belong. */
    for (int i = 1; i < n; i++) {
        syn_view_t *cur = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->focus_seq < cur->focus_seq) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = cur;
    }
    return n;
}

/* Is this window somewhere focus can actually land right now? A window on
 * another desktop or a minimized one is not: focusing it would hand the
 * keyboard to something that is not on screen. */
static bool alttab_view_onscreen(syn_server_t *s, syn_view_t *v)
{
    return !v->minimized && v->workspace &&
           v->workspace->index == s->active_workspace;
}

/* Bring `v` out to where it can be used, whatever that takes, and focus it.
 *
 * Only ever called from alttab_finish(), never mid-cycle. Both halves are
 * things you would notice: switching desktops cross-fades every window on the
 * desk (layout.c workspace_switch), and restoring a minimized window puts it
 * back on screen. Doing either on each Tab press would fire them for every
 * window you merely tabbed *past* — three minimized windows passed on the way
 * to a fourth would all be restored, and a cycle that crossed two desktops
 * would cross-fade the desk twice before you had chosen anything. */
static void alttab_reveal(syn_server_t *s, syn_view_t *v)
{
    /* Desktop first: view_apply_minimized() only actually shows a window whose
     * workspace is visible, so restoring before switching would leave the node
     * disabled and the window minimized-in-fact on arrival. */
    if (v->workspace && v->workspace->index != s->active_workspace)
        workspace_switch(s, v->workspace->index);

    /* Raises and focuses on its own once the workspace is visible; the
     * focus_view below is then a no-op. */
    if (v->minimized)
        view_apply_minimized(s, v, 0);

    if (s->focused_view != v)
        focus_view(s, v, view_surface(v));
}

/* Alt released: commit the window we landed on, which then becomes the most
 * recent so the next Alt+Tab comes back here.
 *
 * The target is re-derived from `depth` rather than remembered from the last
 * press, for the same reason alttab_step() rebuilds the list every time: a
 * pending view pointer is a pointer to dangle. A window that closes between
 * the last Tab and the Alt release therefore shifts what we land on by one —
 * the alternative is a fifth place to remember to clear on destroy. */
static void alttab_finish(syn_server_t *s)
{
    if (!s->alttab.active) return;
    s->alttab.active = false;
    /* Unconditional, not gated on config.alt_tab_preview: turning the preview
     * off mid-cycle would otherwise leave the grid painted on screen forever,
     * and hiding an already-hidden overlay costs nothing. */
    synui_alttab_hide(s);

    syn_view_t *cands[ALTTAB_MAX];
    int n = alttab_candidates(s, cands, ALTTAB_MAX);
    if (n > 0) {
        int idx = ((s->alttab.depth % n) + n) % n;
        alttab_reveal(s, cands[idx]);
    }
    s->alttab.depth = 0;

    if (s->focused_view)
        s->focused_view->focus_seq = ++s->focus_counter;
}

/* One Tab press while Alt is held. dir +1 walks back through the MRU order,
 * -1 (Alt+Shift+Tab) walks forward again. */
static void alttab_step(syn_server_t *s, int dir)
{
    syn_view_t *cands[ALTTAB_MAX];
    int n = alttab_candidates(s, cands, ALTTAB_MAX);
    if (n < 2) {
        /* Nothing to switch to. Mid-cycle this means the windows closed under
         * us, so the grid still on screen is a picture of windows that no
         * longer exist — take it down rather than leave it there until Alt
         * comes up. A no-op when no cycle is running. */
        synui_alttab_hide(s);
        return;
    }

    if (!s->alttab.active) {
        s->alttab.active = true;
        s->alttab.depth  = 0;
    }

    /* Wrap with a floored modulo — C's % keeps the sign, so a bare
     * depth % n sends Alt+Shift+Tab off the front of the list to a negative
     * index on the very first press. */
    s->alttab.depth += dir;
    int idx = ((s->alttab.depth % n) + n) % n;

    /* Only a window already on screen takes focus as you pass over it. One on
     * another desktop or minimized is left exactly where it is until the cycle
     * commits (alttab_reveal) — the grid is what tells you where you are, and
     * it costs nothing to be wrong about for the half-second Alt is down. */
    syn_view_t *target = cands[idx];
    if (alttab_view_onscreen(s, target) && target != s->focused_view)
        focus_view(s, target, view_surface(target));

    /* The tile grid, rebuilt from the same list this press just walked. It is
     * drawn *after* the focus change, so the tile it outlines is the window
     * that is now focused rather than the one that was — and so a focus_view()
     * that bailed out (a view mid-teardown) does not leave the overlay claiming
     * a window the cycle never reached.
     *
     * `cands` is on this stack and stays there: render.c reads it and keeps
     * nothing, which is what lets the cycle go on rebuilding the list from live
     * views every press instead of holding a snapshot to dangle. */
    synui_render_alttab(s, cands, n, idx);
}

static void focus_next(syn_server_t *s, int dir)
{
    syn_workspace_t *ws = server_active_workspace(s);
    if (wl_list_empty(&ws->windows)) return;

    /* Only walk from the focused view if it actually lives on this
     * workspace — its link is threaded through its *own* workspace's list,
     * and mixing lists would run wl_container_of over the wrong sentinel. */
    struct wl_list *target;
    if (!s->focused_view || s->focused_view->workspace != ws) {
        target = dir > 0 ? ws->windows.next : ws->windows.prev;
    } else {
        target = dir > 0 ? s->focused_view->link.next
                         : s->focused_view->link.prev;
        if (target == &ws->windows)
            target = dir > 0 ? ws->windows.next : ws->windows.prev;
    }

    if (target == &ws->windows) return;
    syn_view_t *next = wl_container_of(target, next, link);
    if (next->mapped)
        focus_view(s, next, view_surface(next));
}

/* Hand the child a clean signal environment.
 *
 * Both halves of a process's signal state survive exec: the blocked mask
 * always, and SIG_IGN dispositions (SIG_DFL and handlers are reset, ignores are
 * not). synui has both kinds set, and neither is anything a child should be
 * born with:
 *
 *   - wl_event_loop_add_signal() is built on signalfd, so it *blocks*
 *     SIGINT/SIGTERM/SIGHUP process-wide to keep them off the default
 *     dispositions. Inherited, that makes every app synui launches unkillable
 *     by SIGTERM — `pkill waybar` returns success and nothing happens, and an
 *     app that shuts down on SIGTERM never gets the chance.
 *   - SIGPIPE is SIG_IGN (synui must not die writing to a departed client), and
 *     SIGCHLD carries our reaper. A child that inherits an ignored SIGPIPE sees
 *     EPIPE where it expected to die quietly at the end of a pipeline.
 *
 * This is the third time this family of bug has bitten this project (SIGCHLD
 * into Xwayland, systemd's IgnoreSIGPIPE into the idle-inhibit helper), so:
 * reset the lot, right before exec, for every child. Public because synui forks
 * in three places (here, the cmdbar's CMD:, and autostart) and every one of them
 * needs this — autostart most of all: that is what launches waybar.
 *
 * Every signal, not just the ones synui sets. An ignored disposition can be
 * inherited from anywhere up the chain — systemd's IgnoreSIGPIPE=yes is how it
 * got us the second time, and it was a SIGQUIT ignored by a *test runner* that
 * exposed the hole here. Enumerating the signals we happen to know about would
 * only fix the leaks we already know about. */
void synui_child_reset_signals(void)
{
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    /* SIGKILL and SIGSTOP cannot be reset; signal() just fails on them. */
    for (int sig = 1; sig < NSIG; sig++)
        signal(sig, SIG_DFL);
}

static void spawn(const char *cmd)
{
    if (!cmd || !*cmd) return;
    if (fork() == 0) {
        setsid();
        synui_child_reset_signals();
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(1);
    }
}

/* Public wrapper so dock.c can launch a pinned app's .desktop Exec. */
void synui_spawn(const char *cmd)
{
    spawn(cmd);
}

static uint32_t now_msec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Start menu (Super-tap) ──────────────────────────────── */

/* Open the start menu.
 *
 * Three homes in three years, and the reason keeps being keyboard focus:
 *
 *   waybar   a GTK popup with no IPC to open it, so synui sent the bar surface a
 *            synthetic pointer press and let GTK pop it. It could never be
 *            arrow-navigated: waybar sets keyboard_interactivity NONE once at
 *            startup and never revises it, so its menu is handed no keyboard
 *            focus at all. Three synui-side focus fixes each delivered a
 *            textbook key sequence that GTK ignored — the wall was in waybar.
 *   menu.c   compositor-drawn, because a panel synui draws is one it can hand
 *            the keyboard to. That worked, and cost a hand-rolled .desktop
 *            scanner plus a second panel to theme and maintain.
 *   the bar  quickshell takes EXCLUSIVE keyboard focus properly
 *            (PanelWindow { focusable: true }), so the original objection is
 *            simply gone and the menu lives with the bar it belongs to.
 *
 * synui still OWNS the keystroke — handle_keybinding runs before the focused
 * surface sees anything, so Super tap keeps working regardless of what has
 * focus. It just no longer draws the result. The direction of the call is the
 * whole trick: synctl is request/response with no event stream, so the
 * compositor cannot push to a client, but quickshell's IPC goes client-ward and
 * needs no new protocol here.
 *
 * The focused output is passed along because synui is the only process that
 * knows it — there is no Wayland protocol that tells a layer-shell client which
 * monitor has focus, and the bar would otherwise have to guess or probe back. */
void synui_start_menu_open(syn_server_t *s)
{
    syn_output_t *o = server_focused_output(s);
    const char *name = (o && o->wlr_output && o->wlr_output->name)
                       ? o->wlr_output->name : "";

    /* execvp, not spawn(): spawn() goes through `/bin/sh -c` and this
     * interpolates an output name. Those come from the kernel rather than from
     * a user, but a shell in the path is a shell to get wrong later, and there
     * is nothing here that needs one. */
    if (fork() == 0) {
        setsid();
        synui_child_reset_signals();
        execlp("synui-bar", "synui-bar", "ipc", "call", "menu", "toggle",
               name, (char *)NULL);
        _exit(1);
    }
}

/* Execute a bind action (see config.c for the names and defaults). */
void synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    syn_workspace_t *ws = server_active_workspace(s);

    if (strcmp(action, "spawn") == 0) {
        spawn(arg);
    } else if (strcmp(action, "term") == 0) {
        /*
         * Default config: fall back through common terminals, so a box whose
         * terminal package failed to install still opens SOMETHING rather than
         * a keybind that silently does nothing.
         *
         * The chain is only used when the configured terminal is still the
         * shipped default — an explicit `terminal = <x>` in synuirc is a choice,
         * and quietly launching a different program when it is missing would
         * hide the mistake. foot stays in the chain behind kitty: it is what
         * every system installed before this shipped, and it is 793 KiB against
         * kitty's 65 MiB, so it is also the sensible rescue.
         */
        if (strcmp(s->config.terminal, "kitty") == 0)
            spawn("kitty || foot || alacritty || xterm");
        else
            spawn(s->config.terminal);
    } else if (strcmp(action, "cmdbar") == 0) {
        if (s->cmdbar.visible) cmdbar_hide(s);
        else                   cmdbar_show(s);
    } else if (strcmp(action, "overlay") == 0) {
        overlay_toggle(s);
    } else if (strcmp(action, "screenshot") == 0) {
        /* Capture the monitor the user is actually on. grim can only be told a
         * monitor by name, and nothing outside the compositor knows which one
         * has focus — a bare `grim` grabs the whole layout's bounding box,
         * which across SYNAPSE's three monitors is a 3640x3000 image with dead
         * space in the gaps. Fall back to that only if there is no focus. */
        syn_output_t *o = server_focused_output(s);
        const char *name = (o && o->wlr_output) ? o->wlr_output->name : NULL;
        char cmd[256];
        if (name && *name)
            snprintf(cmd, sizeof(cmd), "synui-screenshot output '%s'", name);
        else
            snprintf(cmd, sizeof(cmd), "synui-screenshot full");
        spawn(cmd);
        /* The shutter, fired here rather than inside synui-screenshot so the
         * region form (which is bound straight to `spawn synui-screenshot
         * region`) is not the odd one out — and so one switch governs it. */
        sound_play(s, SOUND_EVT_SCREENSHOT);
    } else if (strcmp(action, "quit") == 0) {
        /* The logout chime lives in synui_destroy, not here: greetd's SIGTERM
         * and ^C never reach this branch and deserve it just as much. */
        wl_display_terminate(s->display);
    } else if (strcmp(action, "close") == 0) {
        if (s->focused_view) view_close(s->focused_view);
    } else if (strcmp(action, "layout_cycle") == 0) {
        ws->layout = (ws->layout + 1) % 4;
        static const char *lnames[] = {"tiling","floating","monocle","AI"};
        wlr_log(WLR_INFO, "synui: layout → %s", lnames[ws->layout]);
        layout_apply(s, ws);
        /* Say so on screen. Cycling the layout of a desktop whose windows are
         * all floating — which every layout skips — moves nothing at all, so a
         * log line was the only evidence the key had done anything, and the
         * binding read as dead while quietly walking the workspace round to AI.
         * Replaces its own toast (layout_notif_id) rather than stacking one
         * card per press. */
        char lbody[96];
        snprintf(lbody, sizeof(lbody), "Desktop %d — %s",
                 ws->index + 1, ws->name);
        s->layout_notif_id = notif_post(s, "synui", lnames[ws->layout], lbody,
                                        NOTIF_URGENCY_LOW, 1500,
                                        s->layout_notif_id);
    } else if (strcmp(action, "master_shrink") == 0) {
        layout_adjust_master(s, ws, -0.05f);
    } else if (strcmp(action, "master_grow") == 0) {
        layout_adjust_master(s, ws, +0.05f);
    } else if (strcmp(action, "focus_next") == 0) {
        focus_next(s, 1);
    } else if (strcmp(action, "focus_prev") == 0) {
        focus_next(s, -1);
    } else if (strcmp(action, "alt_tab") == 0) {
        alttab_step(s, 1);
    } else if (strcmp(action, "alt_tab_prev") == 0) {
        alttab_step(s, -1);
    } else if (strcmp(action, "alt_tab_commit") == 0) {
        /* What letting go of Alt does. Bound to no key — a real cycle ends on a
         * modifier release, which only keyboard_handle_modifiers can see.
         *
         * It exists because everything the cycle DOES now happens here rather
         * than on the way past (switch desktop, restore a minimized window),
         * and a modifier release cannot be synthesised into a headless synui:
         * the headless backend has no input devices, and uinput would be
         * delivered to the live session instead. Same seam as
         * SYNUI_POWER_SUPPLY_DIR in the lid tests. */
        alttab_finish(s);
    } else if (strcmp(action, "stack_next") == 0) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, 1);
    } else if (strcmp(action, "stack_prev") == 0) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, -1);
    } else if (strcmp(action, "float_toggle") == 0) {
        syn_view_t *v = s->focused_view;
        if (!v) return;
        v->floating = !v->floating;
        /* Reflow the remaining tiled windows first, then place this one. */
        layout_apply(s, ws);
        if (v->floating) {
            layout_float_place(s, v);
            wlr_scene_node_raise_to_top(view_node(v));
        }
    } else if (strcmp(action, "fullscreen_toggle") == 0) {
        /* Force fullscreen on the focused window regardless of whether it ever
         * asked. Games that do "borderless fullscreen" (the KEX engine's
         * v_windowmode 1, and plenty of others) never send a fullscreen
         * request at all — they just ask for an undecorated window and expect
         * the WM to leave its geometry alone, which a tiling compositor by
         * definition does not. Without this there was no way to fullscreen
         * them by hand. view_apply_fullscreen is the shared choke point, so
         * this picks up the output targeting, the buffer rescale, the bar
         * occlusion and the game-mode signal for free. */
        if (s->focused_view)
            view_apply_fullscreen(s, s->focused_view,
                                  !s->focused_view->fullscreen);
    } else if (strcmp(action, "maximize_toggle") == 0) {
        if (!s->focused_view) return;
        view_apply_maximized(s, s->focused_view, !s->focused_view->maximized);
    } else if (strcmp(action, "decorations_toggle") == 0) {
        /* Global, not per-window: the titlebar is chrome the compositor owes
         * every client it told SERVER_SIDE, so it goes away for all of them at
         * once or the desktop is half-decorated. Borders stay — they are the
         * focus indicator, and they are what the grab ring hangs off. */
        deco_toggle_titlebars(s);
    } else if (strcmp(action, "minimize_toggle") == 0) {
        /* Only ever minimizes: a minimized window has its scene node
         * disabled, so it can never hold focus for this to toggle back. */
        if (s->focused_view) view_apply_minimized(s, s->focused_view, 1);
    } else if (strcmp(action, "minimize_restore") == 0) {
        syn_view_t *v;
        wl_list_for_each(v, &ws->windows, link)
            if (v->mapped && v->minimized) {
                view_apply_minimized(s, v, 0);
                break;
            }
    } else if (strcmp(action, "ai_ask") == 0) {
        cmdbar_ask_window(s);
    } else if (strcmp(action, "displays") == 0) {
        dispcfg_toggle(s);
    } else if (strcmp(action, "wallpaper") == 0) {
        wppick_toggle(s);
    } else if (strcmp(action, "cursor") == 0) {
        curpick_toggle(s);
    } else if (strcmp(action, "cursor_reload") == 0) {
        /* What synui-cursor(1) dispatches after writing cursor.state, so a
         * theme installed from a terminal takes effect without a re-login. */
        cursor_reload(s);
    } else if (strcmp(action, "launcher_style") == 0) {
        launcher_toggle_style(s);
    } else if (strcmp(action, "volume") == 0) {
        /* wpctl asks WirePlumber, which owns the sink, rather than poking ALSA
         * behind PipeWire's back. @DEFAULT_AUDIO_SINK@ is resolved at run time,
         * so the knob follows the sink the user actually switched to instead of
         * a device id captured when synui started. -l caps the raise at 100%:
         * without it a spun knob keeps climbing into software gain and clips. */
        if (arg && strcmp(arg, "up") == 0)
            spawn("wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+");
        else if (arg && strcmp(arg, "down") == 0)
            spawn("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
        else if (arg && strcmp(arg, "mute") == 0)
            spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        else
            wlr_log(WLR_ERROR, "synui: volume: bad arg '%s'", arg ? arg : "");
        /* The feedback blip, off by default like every other event sound. Not
         * on mute: a sound to confirm silence is the one case where the sound
         * is the wrong answer. */
        if (arg && strcmp(arg, "mute") != 0)
            sound_play(s, SOUND_EVT_VOLUME);
    } else if (strcmp(action, "power") == 0) {
        power_toggle(s);
    } else if (strcmp(action, "taskmgr") == 0) {
        taskmgr_toggle(s);
    } else if (strcmp(action, "news") == 0) {
        news_toggle(s);
    } else if (strcmp(action, "game") == 0) {
        game_toggle(s);
    } else if (strcmp(action, "cat") == 0) {
        cat_toggle(s);
    } else if (strcmp(action, "lock") == 0) {
        /* The native lock (lock.c). Idempotent, so the idle timer, the power
         * panel's Lock row, the menu's Lock Screen and logind's before-sleep
         * can all reach it the same way. */
        synui_lock(s);
    } else if (strcmp(action, "unlock") == 0) {
        /* Escape hatch: force the native lock off without a password. Reachable
         * only over synui's own control socket, which is the session user's —
         * an attacker at the locked machine cannot get to it without first
         * logging into a TTY (which needs the password anyway). It is the
         * `synctl dispatch unlock` counterpart of `pkill swaylock`, there so a
         * lock bug can never make a hard reboot the only way out. */
        synui_unlock(s);
    } else if (strcmp(action, "ai_backend") == 0) {
        /* Toggle synapd between GPU and CPU inference. The helper owns the
         * work (rewrite the systemd drop-in, record /run/synapd/backend,
         * restart synapd); synui just fires it. The welcome-menu "AI Backend"
         * row reflects the new state the next time the menu is opened. */
        spawn("synui-ai-backend toggle");
    } else if (strcmp(action, "network") == 0) {
        /* Wi-Fi / network configuration. NetworkManager's nmtui is the whole
         * UI here — synui has no text entry to type a passphrase into, so a
         * compositor-native picker would need one built first. Reaching it took
         * knowing it was buried in waybar's SYNAPSE menu; this puts it on a
         * keybind and in the welcome menu. Configurable (network_cmd) so a box
         * running iwd rather than NM can point it somewhere else. */
        spawn(s->config.network_cmd);
    } else if (strcmp(action, "wallpaper_reload") == 0) {
        synui_config_reload(s);
    } else if (strcmp(action, "widgets") == 0) {
        /* Super+Shift+A: the widget manager, one row per widget. It replaced a
         * blind group toggle, exactly as the filter panel replaced one — and,
         * as there, the old behaviour is still one key: Space from any row.
         * `spawn synui-widgets toggle` remains bindable for anyone who wants
         * the panel-less version back. */
        widgets_toggle(s);
    } else if (strcmp(action, "sounds") == 0) {
        /* Super+S: event sounds. Everything is off until turned on here. */
        sound_toggle(s);
    } else if (strcmp(action, "filters") == 0) {
        /* Super+E: the filter panel (sliders for each strength). The blind
         * on/off toggle it replaced is still available as "effects_toggle"
         * for anyone who bound it — the panel's Space key does the same. */
        filters_toggle(s);
    } else if (strcmp(action, "effects_toggle") == 0) {
        /* Runtime on/off for the GLES post-process CRT filters. The pass is
         * gated per-frame on config.effects, so flipping it takes effect on
         * the next commit — but only if the output actually repaints, so
         * damage every output whole (turning the filters *off* otherwise
         * leaves the last post-processed frame on screen). */
        s->config.effects = !s->config.effects;
        wlr_log(WLR_INFO, "synui: filters %s",
                s->config.effects ? "on" : "off");
        syn_output_t *o;
        wl_list_for_each(o, &s->outputs, link) {
            if (o->scene_output)
                wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
            wlr_output_schedule_frame(o->wlr_output);
        }
    } else if (strcmp(action, "welcome_startup") == 0) {
        /* The menu's own "Show At Startup" checkbox. Toggles in place and
         * re-renders so the box visibly ticks — unlike the rows that launch
         * something, there is nothing else to show for it. Persisted, so the
         * choice survives the restart it is about. */
        s->config.welcome_at_startup = !s->config.welcome_at_startup;
        welcome_state_save(&s->config);
        synui_render_welcome(s);
    } else if (strcmp(action, "menu") == 0) {
        if (s->welcome_ui.shown) synui_welcome_hide(s);
        else                     synui_render_welcome(s);
    } else if (strcmp(action, "control") == 0) {
        /* Bare (Super+C) toggles the front door; with a category name it opens
         * onto that category, which is what the start menu's Settings submenu
         * dispatches — see ctlpanel_show_cat. */
        if (arg && *arg) ctlpanel_show_cat(s, arg);
        else             ctlpanel_toggle(s);
    } else if (strcmp(action, "theme") == 0) {
        theme_toggle(s);
    } else if (strcmp(action, "bluetooth") == 0) {
        bt_toggle(s);
    } else if (strcmp(action, "printers") == 0) {
        /* cups ships a complete admin UI on localhost:631; there is no GUI to
         * write. Opening the page is also what starts cupsd — cups.socket has a
         * loopback TCP listener (a drop-in the installer ships), so the URL
         * socket-activates the daemon rather than refusing the connection. The
         * control panel's Printers row and the start menu both land here. */
        synui_spawn("xdg-open http://localhost:631/");
    } else if (strcmp(action, "clock") == 0) {
        /* "Date & Time" — the compositor's clock/time settings panel. */
        clock_toggle(s);
    } else if (strcmp(action, "calendar") == 0) {
        /* Super+Shift+T, and the bar clock's on-click (via synctl dispatch). */
        calendar_toggle(s);
    } else if (strcmp(action, "night_light") == 0) {
        nightlight_toggle(s);
    } else if (strcmp(action, "record") == 0) {
        /* Record the monitor the user is actually on — same reason screenshot
         * does this above. wf-recorder captures one output and, given no name
         * on a multi-monitor layout, prompts on stdin for a menu number; the
         * keybind's child has no terminal, so that read hits EOF and it dies
         * before recording anything. Only the compositor knows the focus. */
        syn_output_t *o = server_focused_output(s);
        const char *name = (o && o->wlr_output) ? o->wlr_output->name : NULL;
        char cmd[256];
        if (name && *name)
            snprintf(cmd, sizeof(cmd), "synui-record --output '%s'", name);
        else
            snprintf(cmd, sizeof(cmd), "synui-record");
        synui_spawn(cmd);
    } else if (strcmp(action, "clipboard") == 0) {
        clipboard_toggle(s);
    } else if (strcmp(action, "brightness_up") == 0) {
        logind_brightness_step(s, +5);
    } else if (strcmp(action, "brightness_down") == 0) {
        logind_brightness_step(s, -5);
    } else if (strcmp(action, "start_menu") == 0) {
        synui_start_menu_open(s);
    } else if (strcmp(action, "ws") == 0) {
        int n = atoi(arg);
        if (n >= 1 && n <= WORKSPACE_MAX)
            workspace_switch(s, n - 1);
    } else if (strcmp(action, "movews") == 0) {
        int n = atoi(arg);
        if (n >= 1 && n <= WORKSPACE_MAX && s->focused_view)
            workspace_move_view(s, s->focused_view, n - 1);
    } else if (strcmp(action, "move_output") == 0) {
        /* Throw the focused window to the next monitor (connection order,
         * cyclic); arg "prev" goes the other way. It stays on its current
         * desktop — only the monitor changes. Tiled/monocle/maximized windows
         * are repositioned by the target output's layout via view_set_output;
         * floating and fullscreen windows carry their own absolute geometry, so
         * translate or re-cover them onto it explicitly. */
        syn_view_t *v = s->focused_view;
        if (!v || !v->mapped) return;
        syn_output_t *cur = v->output ? v->output : server_focused_output(s);
        if (!cur) return;
        bool prev = arg && strcmp(arg, "prev") == 0;
        /* Step one element in the wl_list, wrapping past the head sentinel. */
        struct wl_list *node = prev ? cur->link.prev : cur->link.next;
        if (node == &s->outputs) node = prev ? s->outputs.prev : s->outputs.next;
        syn_output_t *next = wl_container_of(node, next, link);
        if (!next || next == cur) return;                  /* only one monitor */

        struct wlr_box from, to;
        output_box_of(s, cur,  &from);
        output_box_of(s, next, &to);

        view_set_output(s, v, next);

        if (v->fullscreen) {
            v->x = to.x;      v->y = to.y;
            v->w = to.width;  v->h = to.height;
            if (v->is_xwayland)
                wlr_xwayland_surface_configure(v->xsurface, to.x, to.y,
                                               to.width, to.height);
            else
                wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel,
                                          to.width, to.height);
            wlr_scene_node_set_position(view_node(v), to.x, to.y);
            wlr_scene_node_raise_to_top(view_node(v));
        } else if (v->floating) {
            /* Keep the same on-screen position relative to the monitor. */
            int nx = v->x + (to.x - from.x);
            int ny = v->y + (to.y - from.y);
            if (nx + v->w > to.x + to.width)  nx = to.x + to.width  - v->w;
            if (ny + v->h > to.y + to.height) ny = to.y + to.height - v->h;
            if (nx < to.x) nx = to.x;
            if (ny < to.y) ny = to.y;
            view_resize(v, nx, ny, v->w, v->h);
            wlr_scene_node_raise_to_top(view_node(v));
        }
        /* workspace_move_view keeps focus (the target workspace is visible on
         * the new monitor); re-assert activation so the client repaints. */
        focus_view(s, v, view_surface(v));
    } else {
        wlr_log(WLR_ERROR, "synui: unknown bind action '%s'", action);
    }
}

/* Welcome-menu navigation: only unmodified Up/Down/j/k, Enter and Escape are
 * intercepted while the menu is shown; everything else falls through to the
 * bind table and the focused client. */
static bool welcome_menu_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->welcome_ui.shown) return false;
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return false;

    switch (sym) {
    case XKB_KEY_Up:
    case XKB_KEY_k:
        s->welcome_ui.selected =
            (s->welcome_ui.selected + synui_welcome_menu_len - 1)
            % synui_welcome_menu_len;
        synui_render_welcome(s);
        return true;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        s->welcome_ui.selected =
            (s->welcome_ui.selected + 1) % synui_welcome_menu_len;
        synui_render_welcome(s);
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        synui_binding_execute(s, synui_welcome_menu[s->welcome_ui.selected].action,
                        "");
        return true;
    case XKB_KEY_Escape:
        synui_welcome_hide(s);
        return true;
    }
    return false;
}

/* The digit a keypad key stands for, or NoSymbol if it isn't one.
 *
 * The keypad never produces a digit keysym: with NumLock on it sends KP_1..KP_9,
 * and with NumLock off — or, crucially, with *Shift held*, which inverts NumLock
 * — it sends the cursor-key names instead (KP_End for 1, KP_Down for 2, …). A
 * bind written as "super+1" carries XKB_KEY_1, so none of those ever matched and
 * Super+numpad-N was dead while the number row worked. Both sets map back here.
 *
 * Note KP_Prior/KP_Page_Up and KP_Next/KP_Page_Down are the same keysym; only
 * one of each pair can be listed. */
static xkb_keysym_t numpad_digit(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return XKB_KEY_0 + (sym - XKB_KEY_KP_0);

    switch (sym) {
    case XKB_KEY_KP_Insert: return XKB_KEY_0;
    case XKB_KEY_KP_End:    return XKB_KEY_1;
    case XKB_KEY_KP_Down:   return XKB_KEY_2;
    case XKB_KEY_KP_Next:   return XKB_KEY_3;
    case XKB_KEY_KP_Left:   return XKB_KEY_4;
    case XKB_KEY_KP_Begin:  return XKB_KEY_5;
    case XKB_KEY_KP_Right:  return XKB_KEY_6;
    case XKB_KEY_KP_Home:   return XKB_KEY_7;
    case XKB_KEY_KP_Up:     return XKB_KEY_8;
    case XKB_KEY_KP_Prior:  return XKB_KEY_9;
    default:                return XKB_KEY_NoSymbol;
    }
}

/* The keys a volume knob sends. Called out because these have to dispatch
 * ahead of the modal panels — see keyboard_handle_key. */
static bool is_volume_key(xkb_keysym_t sym)
{
    return sym == XKB_KEY_XF86AudioRaiseVolume ||
           sym == XKB_KEY_XF86AudioLowerVolume ||
           sym == XKB_KEY_XF86AudioMute;
}

/* Run the first bind whose (mods, sym) matches. */
static bool bind_dispatch(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    for (int i = 0; i < s->config.bind_count; i++) {
        syn_bind_t *b = &s->config.binds[i];
        if (b->mods == mods && b->sym == sym) {
            synui_binding_execute(s, b->action, b->arg);
            return true;
        }
    }
    return false;
}

static bool handle_keybinding(syn_server_t *s, xkb_keysym_t sym,
                               uint32_t modifiers)
{
    uint32_t mods = modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                                 WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT);

    /* With Shift held, number keys deliver symbols (!@#$…) — map them back to
     * digits so binds like super+shift+1 match what the user wrote. */
    if (mods & WLR_MODIFIER_SHIFT) {
        static const xkb_keysym_t shifted_nums[] = {
            XKB_KEY_exclam, XKB_KEY_at, XKB_KEY_numbersign,
            XKB_KEY_dollar, XKB_KEY_percent, XKB_KEY_asciicircum,
            XKB_KEY_ampersand, XKB_KEY_asterisk, XKB_KEY_parenleft
        };
        for (int i = 0; i < 9; i++) {
            if (sym == shifted_nums[i]) { sym = XKB_KEY_1 + i; break; }
        }
    }

    /* Print sits at shift level 2 under *Alt*, not Shift (its xkb type is
     * PC_ALT_LEVEL2), so alt+print arrives as Sys_Req and would never match a
     * bind written as "alt+print". Nothing wants to bind Sys_Req itself. */
    if (sym == XKB_KEY_Sys_Req)
        sym = XKB_KEY_Print;

    if (bind_dispatch(s, xkb_keysym_to_lower(sym), mods))
        return true;

    /* Nothing bound to the key itself — if it was a keypad key, try the digit it
     * stands for, so the numpad drives Super+1..9 like the number row. Second,
     * not first, so an explicit "super+kp_1" bind still wins over the fallback. */
    xkb_keysym_t digit = numpad_digit(sym);
    if (digit != XKB_KEY_NoSymbol && bind_dispatch(s, digit, mods))
        return true;

    return false;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    syn_keyboard_t *kb = wl_container_of(listener, kb, key);
    syn_server_t *s = kb->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *wlr_kb = kb->wlr_keyboard;

    notify_activity(s);

    /* Translate keycode to keysym */
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(wlr_kb->xkb_state, keycode, &syms);
    uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_kb);

    bool handled = false;

    /* ── VT switch (Ctrl+Alt+F1..F12) ──────────────────────────────────
     *
     * Deliberately handled BEFORE the locked check below, and before every
     * other binding. This is the escape hatch, and the case it exists for is
     * precisely the one where the session is locked: swaylock is spawned with
     * `-c 000000`, so a lock that will not take your password is an entirely
     * black screen with no way off it. synui had no VT switch at all, so
     * Ctrl+Alt+F2 did nothing and a hard reboot was the only exit. That is not
     * hypothetical — three rejected passwords in a row trip pam_faillock
     * (deny=3, unlock_time=600), after which the *correct* password is refused
     * for ten minutes and the screen looks identical.
     *
     * Safe while locked: the TTY has its own login, and this session stays
     * locked behind us. Every other compositor does this for the same reason.
     */
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && s->session) {
        for (int i = 0; i < nsyms; i++) {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 &&
                syms[i] <= XKB_KEY_XF86Switch_VT_12) {
                unsigned vt = syms[i] - XKB_KEY_XF86Switch_VT_1 + 1;
                wlr_log(WLR_INFO, "synui: switching to VT %u", vt);
                wlr_session_change_vt(s->session, vt);
                return;
            }
        }
    }

    /* While the session is locked, compositor bindings are disabled and no key
     * may reach a window. The native lock (lock.c) draws its own screen and
     * takes the key here; an ext-session-lock client (swaylock) instead holds
     * keyboard focus and is sent the key through the seat. Either way we return
     * — this is below the VT-switch escape above, which must survive both. */
    if (s->locked) {
        if (s->nlock.active) {
            if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                xkb_keysym_t sym = nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol;
                uint32_t cp = nsyms == 1 ? xkb_keysym_to_utf32(sym) : 0;
                lock_handle_key(s, sym, cp);
            }
            return;                 /* swallow presses and releases alike */
        }
        wlr_seat_set_keyboard(s->seat, wlr_kb);
        wlr_seat_keyboard_notify_key(s->seat, event->time_msec,
                                      event->keycode, event->state);
        return;
    }

    /* ── Volume keys ──────────────────────────────────────────────────
     *
     * Dispatched ahead of the command bar and every modal panel, because all
     * of those swallow unmodified keys wholesale (`default: return 1`) — and a
     * knob that stops working because the power panel happens to be open is not
     * a knob. Hoisting is safe: these keysyms exist only on media keys, so
     * jumping the queue cannot shadow a Super+… bind or steal a typed
     * character from the cmdbar's text entry.
     *
     * Below the `s->locked` check on purpose: while locked, synui disables its
     * bindings by contract and keys belong to the lock surface.
     */
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++)
            if (is_volume_key(syms[i]) && bind_dispatch(s, syms[i], 0))
                return;
    }

    /* Command bar absorbs all input when open */
    if (s->cmdbar.visible && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++)
            cmdbar_key(s, syms[i]);
        return;
    }

    /* Super-tap opens the start menu (see syn_server::super_armed). Super is
     * first and foremost a modifier, so the tap is defined by what did *not*
     * happen: armed on a bare Super press, disarmed by any other key here and
     * by any pointer button in server_cursor_button. Only a Super release that
     * survives both is a tap.
     *
     * The release is deliberately not swallowed — it falls through to the
     * normal forwarding path below. Returning early here would leave the
     * focused client holding a Super it never saw released, i.e. a stuck
     * modifier for as long as it keeps focus. */
    bool is_super = false;
    for (int i = 0; i < nsyms; i++)
        if (syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R)
            is_super = true;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* A Super pressed while another modifier is already down is someone
         * building a chord, not tapping. */
        s->super_armed = is_super &&
            !(modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT |
                           WLR_MODIFIER_SHIFT));
    } else if (is_super && s->super_armed) {
        s->super_armed = 0;
        synui_start_menu_open(s);
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* Escape dismisses the dock context menu. */
        if (s->dockmenu.visible) {
            for (int i = 0; i < nsyms; i++)
                if (syms[i] == XKB_KEY_Escape) { dockmenu_close(s); return; }
        }
        /* …and the desktop one. */
        if (s->deskmenu.visible) {
            for (int i = 0; i < nsyms; i++)
                if (syms[i] == XKB_KEY_Escape) { deskmenu_close(s); return; }
        }

        /* Display settings panel: modal for unmodified keys; modified
         * combos (Super+…) fall through to the bind table below. */
        bool absorbed = false;
        for (int i = 0; i < nsyms; i++)
            if (dispcfg_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Wallpaper selector: same modal contract as the display panel. */
        for (int i = 0; i < nsyms; i++)
            if (wppick_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Cursor theme picker: same modal contract as the wallpaper one. */
        for (int i = 0; i < nsyms; i++)
            if (curpick_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Power saving panel: same modal contract as the display panel. */
        for (int i = 0; i < nsyms; i++)
            if (power_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Task manager: same modal contract, except that it also claims bare
         * Shift, since Shift+X is its SIGKILL. Super+… still falls through. */
        for (int i = 0; i < nsyms; i++)
            if (taskmgr_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* News: modal like the task manager, and it claims bare Shift too —
         * its '/' search box types capitals. */
        for (int i = 0; i < nsyms; i++)
            if (news_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* CRT filter panel: same modal contract as the power panel. */
        for (int i = 0; i < nsyms; i++)
            if (filters_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Desktop widget manager: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (widgets_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Event sounds panel: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (sound_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Clock & Time settings panel: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (clock_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Calendar popup: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (calendar_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Control panel: same modal contract again. */
        for (int i = 0; i < nsyms; i++)
            if (ctlpanel_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Theme manager: same modal contract again. */
        for (int i = 0; i < nsyms; i++)
            if (theme_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Clipboard history: same modal contract as the rest. */
        for (int i = 0; i < nsyms; i++)
            if (clipboard_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Bluetooth: same modal contract. Ahead of the start menu because a
         * pairing prompt has BlueZ blocked on the answer. */
        for (int i = 0; i < nsyms; i++)
            if (bt_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* No start-menu branch here any more. The menu is a layer-shell client
         * with EXCLUSIVE keyboard focus (quickshell/StartMenu.qml), so its keys
         * arrive by the ordinary focus path — synui does not have to intercept
         * them, and must not: swallowing them here is what would make it deaf. */

        /* Welcome menu: intercepts only its navigation keys. */
        for (int i = 0; i < nsyms; i++)
            if (welcome_menu_key(s, syms[i], modifiers))
                return;
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            if (handle_keybinding(s, syms[i], modifiers)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        /* Compositor keybinds win; then the IME, if it grabbed the keyboard —
         * typing "nihao" has to reach fcitx5 to be composed, not land in the
         * text field as five Latin letters. Only then does the client see it. */
        if (ime_handle_key(s, wlr_kb, event->time_msec,
                           event->keycode, event->state))
            return;

        wlr_seat_set_keyboard(s->seat, wlr_kb);
        wlr_seat_keyboard_notify_key(s->seat, event->time_msec,
                                      event->keycode, event->state);
    }
}

/* Re-advertise what the seat can do.
 *
 * Must be called from every path that adds or drops a keyboard, not just from
 * server_new_input: a *virtual* keyboard (wtype, an on-screen keyboard) arrives
 * via virtual-keyboard-v1 and never passes through the backend's new_input, so
 * computing capabilities only there left the seat advertising pointer-only. A
 * client that sees no keyboard capability never calls wl_seat.get_keyboard, so
 * it has no wl_keyboard to send anything to — on a session whose only keyboard
 * is virtual, that means no client can receive a keystroke at all. The pointer
 * bit is unconditional for the reason server_init gives: the compositor always
 * drives a cursor, and a capability-less seat kills clients that call
 * get_pointer.
 */
static void seat_update_capabilities(syn_server_t *s)
{
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&s->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (s->touch_devices > 0)
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(s->seat, caps);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
    syn_keyboard_t *kb = wl_container_of(listener, kb, destroy);
    syn_server_t *s = kb->server;
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
    /* After the unlink: the last keyboard leaving drops the capability. */
    seat_update_capabilities(s);
}

/*
 * Lock the NumLock modifier on. A keymap that has just been compiled comes
 * with an xkb state in which nothing is locked, so the numpad emits arrows and
 * Home/End until someone presses NumLock — every login, and again after every
 * SIGHUP reload, which recompiles the keymap. It bites hardest on the lock
 * screen, where swaylock is the only thing on screen and the numpad is how a
 * digit-heavy password gets typed.
 *
 * wlroots derives the keyboard LEDs from this same xkb state, so the NumLock
 * indicator follows for free on keyboards that have one.
 */
static void keyboard_lock_numlock(struct wlr_keyboard *wlr_kb)
{
    xkb_mod_index_t idx =
        xkb_keymap_mod_get_index(wlr_kb->keymap, XKB_MOD_NAME_NUM);
    if (idx == XKB_MOD_INVALID) return;    /* keymap has no NumLock */

    struct wlr_keyboard_modifiers *m = &wlr_kb->modifiers;
    wlr_keyboard_notify_modifiers(wlr_kb, m->depressed, m->latched,
                                  m->locked | ((xkb_mod_mask_t)1 << idx),
                                  m->group);
}

/* Compile the synuirc keymap (xkb_layout/variant/…) and apply it plus the
 * repeat settings to one keyboard; empty fields fall through to the
 * XKB_DEFAULT_* environment and the system default. Shared by device attach
 * and SIGHUP config reload. */
static void keyboard_apply_config(syn_server_t *s, struct wlr_keyboard *wlr_kb)
{
    syn_config_t *cfg = &s->config;
    struct xkb_rule_names names = {
        .rules   = cfg->xkb_rules[0]   ? cfg->xkb_rules   : NULL,
        .model   = cfg->xkb_model[0]   ? cfg->xkb_model   : NULL,
        .layout  = cfg->xkb_layout[0]  ? cfg->xkb_layout  : NULL,
        .variant = cfg->xkb_variant[0] ? cfg->xkb_variant : NULL,
        .options = cfg->xkb_options[0] ? cfg->xkb_options : NULL,
    };
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names,
                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        wlr_log(WLR_ERROR, "synui: configured keymap '%s/%s' failed to "
                "compile — using default", cfg->xkb_layout, cfg->xkb_variant);
        keymap = xkb_keymap_new_from_names(ctx, NULL,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    wlr_keyboard_set_keymap(wlr_kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(wlr_kb, cfg->repeat_rate, cfg->repeat_delay);

    /* After set_keymap, which is what resets the lock to begin with. */
    if (cfg->numlock)
        keyboard_lock_numlock(wlr_kb);
}

static void server_new_keyboard(syn_server_t *s, struct wlr_input_device *dev)
{
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(dev);
    syn_keyboard_t *kb = calloc(1, sizeof(*kb));
    kb->server = s;
    kb->wlr_keyboard = wlr_kb;

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_kb->events.key, &kb->key);
    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&dev->events.destroy, &kb->destroy);

    /* Listeners first: applying the config locks NumLock, and the modifier
     * event that carries it has to reach the seat like any other. */
    keyboard_apply_config(s, wlr_kb);

    wlr_seat_set_keyboard(s->seat, wlr_kb);
    wl_list_insert(&s->keyboards, &kb->link);
    seat_update_capabilities(s);
}

/* A virtual keyboard (wtype, or anything else speaking virtual-keyboard-v1)
 * wraps a real struct wlr_keyboard, so it takes the exact same path a
 * physical keyboard does — this is what lets the waybar menu trigger
 * compositor keybinds (e.g. the activity overview) with no bespoke IPC. */
static void server_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vkb = data;
    server_new_keyboard(s, &vkb->keyboard.base);
}

/* The manager is owned by the display and torn down during wl_display_destroy.
 * wlroots asserts nobody is still subscribed to new_virtual_keyboard when that
 * happens, so drop our listeners here to keep shutdown clean (exit 0). */
static void server_vkb_mgr_destroy(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, vkb_mgr_destroy);
    wl_list_remove(&s->new_virtual_keyboard.link);
    wl_list_remove(&s->vkb_mgr_destroy.link);
}

/* ── Interactive move / resize (Super + mouse drag) ──────── */
/*
 * Begin an interactive grab of `view`. Tiled windows are auto-floated so the
 * drag doesn't fight the layout engine; the workspace reflows around them.
 * For a resize we grab from whichever corner the cursor is nearest.
 */
/* edges: which WLR_EDGE_* a RESIZE drags. 0 = derive from the cursor's quadrant
 * (Super+right-drag, which has no edge to speak of); a border press passes the
 * edge it actually landed on. Ignored for MOVE. */
/* How far the pointer must travel before an armed titlebar press counts as a
 * drag. Below this a press and release is just a click. */
#define GRAB_DRAG_SLOP 5.0

/*
 * Release a view from whatever is pinning its geometry — maximized, snapped or
 * tiled — so a grab can carry it freely. Split out of begin_interactive_edges
 * because an armed titlebar grab runs it later, on the first real motion,
 * rather than at press time.
 */
static void grab_release_constraints(syn_server_t *s, syn_view_t *view,
                                     syn_cursor_mode_t mode)
{
    /* Dragging a maximized window restores it and takes it with you, the way
     * every other desktop does — otherwise it would slide around full-size. */
    if (view->maximized) {
        view_apply_maximized(s, view, 0);
        if (mode == SYNUI_CURSOR_MOVE) {
            /* Re-centre the restored window under the cursor so the grab point
             * stays on the titlebar it was grabbed by. */
            int nx = (int)s->cursor->x - view->w / 2;
            int ny = view->y;
            view_resize(view, nx, ny, view->w, view->h);
        }
    }

    /* Same for a snapped window: a move drags it out of its zone and back to the
     * size it had before it was snapped. A *resize* leaves the snap flag alone
     * on purpose — pulling a snapped window's edge is how you rebalance a split,
     * not how you un-snap it. */
    if (view->snapped && mode == SYNUI_CURSOR_MOVE)
        snap_release_view(s, view, 1);

    if (!view->floating) {
        view->floating = 1;
        layout_apply(s, view->workspace);   /* reflow remaining tiled windows */
    }
}

/* armed: hold off on grab_release_constraints until the pointer has moved
 * GRAB_DRAG_SLOP. Only a titlebar press does this; a Super+drag or a CSD
 * client's xdg_toplevel.move already means "the drag has begun". */
static void begin_interactive_armed(syn_view_t *view, syn_cursor_mode_t mode,
                                    uint32_t edges, bool armed)
{
    syn_server_t *s = view->server;
    if (!view->mapped || view->fullscreen) return;

    s->grab_armed = armed;
    if (armed) {
        s->grab_press_x = s->cursor->x;
        s->grab_press_y = s->cursor->y;
    } else {
        grab_release_constraints(s, view, mode);
    }

    wlr_scene_node_raise_to_top(view_node(view));
    focus_view(s, view, view_surface(view));

    s->grabbed_view = view;
    s->cursor_mode  = mode;

    if (mode == SYNUI_CURSOR_MOVE) {
        s->grab_x = s->cursor->x - view->x;
        s->grab_y = s->cursor->y - view->y;
    } else {
        /* Anchor the drag and pick edges from the cursor's quadrant. */
        s->grab_x = s->cursor->x;
        s->grab_y = s->cursor->y;
        s->grab_geobox = (struct wlr_box){ view->x, view->y, view->w, view->h };
        if (!edges) {
            edges |= (s->cursor->x < view->x + view->w / 2)
                         ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
            edges |= (s->cursor->y < view->y + view->h / 2)
                         ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
        }
        s->resize_edges = edges;
    }

    /* Hold one cursor for the whole grab. It has to be pinned rather than
     * recomputed from the pointer's position: the pointer routinely runs past the
     * edge it is dragging (a client clamps at its minimum size and the cursor
     * keeps going), and it is over the client's own surface for the whole of a
     * move — either would otherwise hand the cursor straight back mid-drag. */
    cursor_set_deco(s, deco_grab_cursor(s, mode, s->resize_edges), now_msec());
}

static void begin_interactive_edges(syn_view_t *view, syn_cursor_mode_t mode,
                                    uint32_t edges)
{
    begin_interactive_armed(view, mode, edges, false);
}

static void begin_interactive(syn_view_t *view, syn_cursor_mode_t mode)
{
    begin_interactive_edges(view, mode, 0);
}

/* Public entry point (synui.h): a CSD client asks us to run the grab itself.
 * Firefox binds no xdg-decoration, draws its own frame, and hit-tests its own
 * shadow margin for the resize edges — so its corners and titlebar arrive here
 * as xdg_toplevel.move/.resize and nowhere else. */
void view_begin_interactive(syn_view_t *view, syn_cursor_mode_t mode,
                            uint32_t edges)
{
    begin_interactive_edges(view, mode, edges);
}

static void process_cursor_move(syn_server_t *s)
{
    syn_view_t *v = s->grabbed_view;

    /* An armed titlebar press is not yet a drag. Hold the window in its
     * maximized/snapped/tiled geometry until the pointer has actually
     * travelled, so a plain click to focus no longer restores it. */
    if (s->grab_armed) {
        double px = s->cursor->x - s->grab_press_x;
        double py = s->cursor->y - s->grab_press_y;
        if (px * px + py * py < GRAB_DRAG_SLOP * GRAB_DRAG_SLOP)
            return;

        s->grab_armed = false;
        grab_release_constraints(s, v, SYNUI_CURSOR_MOVE);
        /* Re-anchor: grab_release_constraints may have resized and moved the
         * window out from under the offsets taken at press time. */
        s->grab_x = s->cursor->x - v->x;
        s->grab_y = s->cursor->y - v->y;
    }

    v->x = (int)(s->cursor->x - s->grab_x);
    v->y = (int)(s->cursor->y - s->grab_y);
    wlr_scene_node_set_position(view_node(v), v->x, v->y);
    view_update_decorations(v);

    /* Arm (and preview) the snap zone the cursor is currently over. The window
     * only actually moves there on release. */
    snap_drag_motion(s, s->cursor->x, s->cursor->y);
}

static void process_cursor_resize(syn_server_t *s)
{
    syn_view_t *v = s->grabbed_view;
    struct wlr_box g = s->grab_geobox;
    double dx = s->cursor->x - s->grab_x;
    double dy = s->cursor->y - s->grab_y;

    int left = g.x, right = g.x + g.width;
    int top  = g.y, bottom = g.y + g.height;

    if (s->resize_edges & WLR_EDGE_LEFT)   left   = g.x + (int)dx;
    else if (s->resize_edges & WLR_EDGE_RIGHT)  right  = g.x + g.width  + (int)dx;
    if (s->resize_edges & WLR_EDGE_TOP)    top    = g.y + (int)dy;
    else if (s->resize_edges & WLR_EDGE_BOTTOM) bottom = g.y + g.height + (int)dy;

    /* Honour the client's min/max size, with a hard floor so a window can
     * never collapse to nothing. Clamp against the edge being dragged so the
     * opposite edge stays anchored. */
    int min_w = 0, min_h = 0, max_w = 0, max_h = 0;
    if (v->is_xwayland) {
        xcb_size_hints_t *sh = v->xsurface->size_hints;
        if (sh) {
            min_w = sh->min_width;  min_h = sh->min_height;
            max_w = sh->max_width;  max_h = sh->max_height;
        }
    } else {
        struct wlr_xdg_toplevel *top_l = v->xdg_surface->toplevel;
        min_w = top_l->current.min_width;   min_h = top_l->current.min_height;
        max_w = top_l->current.max_width;   max_h = top_l->current.max_height;
    }
    if (min_w < 0) min_w = 0;
    if (min_h < 0) min_h = 0;
    if (max_w < 0) max_w = 0;
    if (max_h < 0) max_h = 0;
    int bw = s->config.border_width;
    if (min_w < 2 * bw + 20) min_w = 2 * bw + 20;
    if (min_h < 2 * bw + 20) min_h = 2 * bw + 20;

    int w = right - left, h = bottom - top;
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;
    if (max_w && w > max_w) w = max_w;
    if (max_h && h > max_h) h = max_h;

    if (s->resize_edges & WLR_EDGE_LEFT)  left = right - w;
    else                                   right = left + w;
    if (s->resize_edges & WLR_EDGE_TOP)   top  = bottom - h;
    else                                   bottom = top + h;

    view_resize(v, left, top, w, h);
}

/* ── Pointer ─────────────────────────────────────────────── */
/* Apply synuirc device options to a libinput-backed device. Each option is
 * only touched if set in the config and supported by the device. */
static void input_apply_libinput_config(syn_server_t *s,
                                        struct wlr_input_device *dev)
{
    if (!wlr_input_device_is_libinput(dev)) return;
    struct libinput_device *li = wlr_libinput_get_device_handle(dev);
    if (!li) return;
    syn_config_t *cfg = &s->config;

    if (cfg->tap_to_click >= 0 &&
        libinput_device_config_tap_get_finger_count(li) > 0)
        libinput_device_config_tap_set_enabled(li, cfg->tap_to_click
            ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);

    if (cfg->natural_scroll >= 0 &&
        libinput_device_config_scroll_has_natural_scroll(li))
        libinput_device_config_scroll_set_natural_scroll_enabled(
            li, cfg->natural_scroll);

    if (cfg->left_handed >= 0 &&
        libinput_device_config_left_handed_is_available(li))
        libinput_device_config_left_handed_set(li, cfg->left_handed);

    if (cfg->accel_speed_set &&
        libinput_device_config_accel_is_available(li))
        libinput_device_config_accel_set_speed(li, cfg->accel_speed);
}

/* ── Compositor-owned cursor image ───────────────────────── */
/*
 * Over its own chrome — and for the length of a move/resize grab — the
 * compositor names the cursor, not the client: a corner of the grab ring has to
 * show the diagonal arrow that says what the press will actually do.
 *
 * Giving it back needs the forced re-enter below. A client only calls
 * wl_pointer.set_cursor when it receives an enter, so where the client keeps
 * pointer focus while we hold the cursor — which is every grab, since the
 * implicit pointer grab pins focus to the surface that took the button-down —
 * simply dropping our image leaves the resize arrow on screen until the client
 * next happens to change its own cursor for unrelated reasons. That was the
 * cursor "not switching back" after leaving an edge. Clearing focus and
 * re-entering makes the client re-set it now.
 */
void cursor_set_deco(syn_server_t *s, const char *name, uint32_t time_msec)
{
    if (name) {
        if (s->deco_cursor == name) return;   /* literals: compare by pointer */
        s->deco_cursor = name;
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, name);
        return;
    }

    if (!s->deco_cursor) return;              /* the client already owns it */
    s->deco_cursor = NULL;

    double sx, sy;
    struct wlr_surface *surface =
        surface_at(s, s->cursor->x, s->cursor->y, NULL, &sx, &sy);

    if (surface && surface == s->seat->pointer_state.focused_surface &&
        s->seat->pointer_state.button_count == 0) {
        wlr_seat_pointer_notify_clear_focus(s->seat);
        wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
        return;                               /* the client sets it from here */
    }

    /* No focused client to ask (or focus is about to change anyway, which sends
     * an enter of its own): show the arrow rather than a stale resize cursor. */
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
}

/* Give pointer focus to whatever lies under the cursor and (de)activate the
 * pointer constraint owned by that surface. Public: workspace_switch's
 * jump-focus warps the cursor and needs the focus re-derived immediately
 * rather than on the next motion event. */
void pointer_update_focus(syn_server_t *s, uint32_t time_msec)
{
    /* Mid-click: the Wayland implicit-grab rule requires motion/release to
     * keep targeting whatever surface got the button-down, not whatever the
     * cursor is over right now. Recomputing focus here can yank it away from
     * a popup mid-click (e.g. if the popup's scene node was briefly resized
     * by a client-side recommit), losing the matching release — GTK/XUL
     * menus then look like clicking an item "does nothing".
     *
     * The focus must stay pinned, but motion still has to be delivered, or a
     * client that tracks a drag (text selection, sliders, slurp's region
     * select) sees the press and the release with nothing in between. Send it
     * against the grab surface's origin rather than re-deriving coordinates
     * from whatever is under the cursor now — the cursor is routinely dragged
     * off the grab surface and even off-screen. */
    if (s->seat->pointer_state.button_count > 0) {
        if (s->seat->pointer_state.focused_surface)
            wlr_seat_pointer_notify_motion(s->seat, time_msec,
                                           s->cursor->x - s->ptr_grab_off_x,
                                           s->cursor->y - s->ptr_grab_off_y);
        return;
    }

    double sx, sy;
    struct wlr_surface *surface =
        surface_at(s, s->cursor->x, s->cursor->y, NULL, &sx, &sy);
    if (surface) {
        wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
    } else {
        /* Nothing under the cursor but our own chrome (borders and the grab ring
         * are scene rects, not surfaces, so they land here on every motion). The
         * arrow is right for the wallpaper — but not over a resize edge, where
         * deco_hover_update has just claimed the cursor, and this would undo it. */
        if (!s->deco_cursor)
            wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
        wlr_seat_pointer_notify_clear_focus(s->seat);
    }
    constraints_focus_surface(s, surface);
}

/*
 * Re-derive pointer focus after the scene changed under a cursor that did not
 * move. Every other caller of pointer_update_focus() is driven by motion, so
 * without this a window that maps (or unmaps, exposing what was behind it)
 * beneath a stationary pointer never receives wl_pointer.enter — the client
 * only finds out it has the pointer once the user physically nudges the mouse.
 *
 * That is not merely cosmetic for clients that want the pointer at startup.
 * SDL only engages a pointer lock once its window holds pointer focus, so it
 * reports SDL_SetRelativeMouseMode(true) as SUCCEEDING while sending no
 * lock_pointer request at all: the client believes it grabbed the pointer and
 * has not. Verified with a fullscreen SDL window mapped under a still cursor —
 * zero enter events in three seconds.
 *
 * Timestamp: these events are not input-driven, so there is no hardware time to
 * quote. CLOCK_MONOTONIC matches what wlroots feeds the seat elsewhere.
 */
static void pointer_rebase_handler(void *data)
{
    syn_server_t *s = data;
    s->pointer_rebase_idle = NULL;
    if (s->shutting_down) return;
    /* Mid-click the implicit grab pins focus anyway, and pointer_update_focus
     * would only re-send motion against the grab surface — skip the churn. */
    if (s->seat->pointer_state.button_count > 0) return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pointer_update_focus(s, (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}

void pointer_rebase(syn_server_t *s)
{
    if (s->shutting_down) return;
    if (s->pointer_rebase_idle) return;   /* already queued for this dispatch */

    /*
     * Deferred to an idle, and that is the whole trick. Calling
     * pointer_update_focus() straight from a map handler finds NOTHING under
     * the cursor: wlr_scene subscribes to the surface commit too, and on the
     * commit that maps a window synui's map listener runs first, so the scene
     * buffer node the hit test needs does not exist yet. surface_at() returns
     * NULL, pointer focus is cleared, and the bug looks unfixed. An idle runs
     * once the whole dispatch is done and the scene is current.
     *
     * It also coalesces: mapping one window can fire several of these (map,
     * layout, occlusion), and they would all compute the same answer.
     */
    s->pointer_rebase_idle = wl_event_loop_add_idle(
        wl_display_get_event_loop(s->display), pointer_rebase_handler, s);
}

/* ── The pointer's modal chain ───────────────────────────────
 *
 * The same panels the keyboard chain in keyboard_handle_key walks, in the same
 * order, for the same reason: exactly one of them is up at a time, each one
 * claims the event while it is, and the order settles the rare case where two
 * are. Each handler answers 1 if it was open and took the event.
 *
 * THE LIST IS WRITTEN ONCE. Four things walk it — motion, button, wheel, and
 * "is anything open" — and four hand-kept copies is four chances to add a panel
 * to three of them. This is the bug the control panel's item table was built to
 * stop happening to settings rows; the same argument applies here.
 *
 * X(prefix, member): `prefix` names the panel's three handlers, `member` its
 * struct in syn_server_t. They differ for two of them (calendar/cal,
 * theme/thememgr), which is exactly why both are spelled out.
 *
 * Bluetooth is NOT here: it had a pointer long before this contract existed and
 * keeps its own wiring in pointer_button below, where the pairing-prompt case
 * (BlueZ is blocked on the answer, so a click must not dismiss it) already
 * lives. Nor are the dock and desktop menus, for the same reason.
 */
#define SYN_PANEL_LIST(X) \
    X(dispcfg,  dispcfg)  \
    X(wppick,   wppick)   \
    X(curpick,  curpick)  \
    X(power,    power)    \
    X(taskmgr,  taskmgr)  \
    X(news,     news)     \
    X(filters,  filters)  \
    X(widgets,  widgets)  \
    X(sound,    sound)    \
    X(clock,    clock)    \
    X(calendar, cal)      \
    X(ctlpanel, ctlpanel) \
    X(theme,    thememgr) \
    X(clipboard, clipboard)

/* Is any of them open? Asked where there is nothing to hand the event to but it
 * still must not reach the window underneath — a horizontal wheel, and the
 * release of a click the panel already swallowed the press of. */
static bool panel_pointer_active(syn_server_t *s)
{
    /* The command bar is not in the list — it has no rows, so no _motion and no
     * _scroll — but it is just as modal, and the two things this answers (swallow
     * the wheel, swallow the stray release) apply to it identically. */
    if (s->cmdbar.visible) return true;
#define X(fn, mem) if (s->mem.visible) return true;
    SYN_PANEL_LIST(X)
#undef X
    return false;
}

static bool panel_pointer_motion(syn_server_t *s, double lx, double ly)
{
#define X(fn, mem) if (fn##_motion(s, lx, ly)) return true;
    SYN_PANEL_LIST(X)
#undef X
    return false;
}

static bool panel_pointer_click(syn_server_t *s, double lx, double ly,
                                uint32_t button, uint32_t time_msec)
{
#define X(fn, mem) if (fn##_click(s, lx, ly, button, time_msec)) return true;
    SYN_PANEL_LIST(X)
#undef X
    return false;
}

static bool panel_pointer_scroll(syn_server_t *s, double lx, double ly,
                                 double delta)
{
#define X(fn, mem) if (fn##_scroll(s, lx, ly, delta)) return true;
    SYN_PANEL_LIST(X)
#undef X
    return false;
}

/* Shared relative-motion path: broadcast the raw delta to relative-pointer
 * clients, let an active constraint absorb (locked) or clamp (confined) the
 * move, then move the cursor and update pointer focus. */
static void process_pointer_motion(syn_server_t *s, uint32_t time_msec,
                                   struct wlr_input_device *device,
                                   double dx, double dy,
                                   double unaccel_dx, double unaccel_dy)
{
    notify_activity(s);

    /* Moving the mouse wakes the lock screen. The rest of the function still
     * runs so the cursor tracks, but focus never reaches a window while locked
     * (guarded below), so nothing underneath sees the motion. */
    if (s->nlock.active)
        lock_notify_activity(s);

    if (s->relative_pointer_mgr)
        wlr_relative_pointer_manager_v1_send_relative_motion(
            s->relative_pointer_mgr, s->seat,
            (uint64_t)time_msec * 1000, dx, dy, unaccel_dx, unaccel_dy);

    if (s->cursor_mode == SYNUI_CURSOR_PASSTHROUGH &&
        constraints_apply_motion(s, &dx, &dy))
        return;   /* locked pointer: the cursor stays put */

    wlr_cursor_move(s->cursor, device, dx, dy);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* An in-flight DnD icon rides the cursor (tree is empty otherwise). */
    wlr_scene_node_set_position(&s->drag_icon_tree->node,
                                (int)s->cursor->x, (int)s->cursor->y);

    if (s->cursor_mode == SYNUI_CURSOR_MOVE)   { process_cursor_move(s);   return; }
    if (s->cursor_mode == SYNUI_CURSOR_RESIZE) { process_cursor_resize(s); return; }

    /* Dock drag-to-reposition floats the bar under the cursor. */
    if (s->dock_drag.active) {
        dock_drag_motion(s, s->cursor->x, s->cursor->y);
        return;
    }
    /* Same for a desktop icon being dragged to a new cell. */
    if (s->deskicon_drag.active) {
        deskicon_drag_motion(s, s->cursor->x, s->cursor->y);
        return;
    }
    /* Context menu hover highlight follows the cursor. */
    if (s->dockmenu.visible)
        dockmenu_motion(s, s->cursor->x, s->cursor->y);
    if (s->deskmenu.visible)
        deskmenu_motion(s, s->cursor->x, s->cursor->y);

    if (s->bt.visible)
        bt_motion(s, s->cursor->x, s->cursor->y);

    /* An open settings panel takes the motion and the routine below it does not
     * run. A modal panel is drawn over whatever is under the cursor, so lighting
     * up a titlebar button or a hover state the click can never reach would be
     * feedback for something that is not going to happen. */
    if (panel_pointer_motion(s, s->cursor->x, s->cursor->y))
        return;

    /* Let the auto-hide dock react to the cursor reaching its edge. */
    dock_pointer_motion(s);

    /* Light up the titlebar button under the pointer (repaints only on change),
     * and show the resize arrow for the edge it would drag. */
    deco_hover_update(s, s->cursor->x, s->cursor->y, time_msec);

    pointer_update_focus(s, time_msec);
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    process_pointer_motion(s, event->time_msec, &event->pointer->base,
                           event->delta_x, event->delta_y,
                           event->unaccel_dx, event->unaccel_dy);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    /* Convert to a delta so constraints and relative-pointer clients keep
     * working on absolute devices (VMs, some tablets). */
    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(s->cursor, &event->pointer->base,
                                         event->x, event->y, &lx, &ly);
    process_pointer_motion(s, event->time_msec, &event->pointer->base,
                           lx - s->cursor->x, ly - s->cursor->y,
                           lx - s->cursor->x, ly - s->cursor->y);
}

/* Does the seat still hold this button down — i.e. was its press forwarded to a
 * client? Only then may a release be sent, or the client sees a release it has no
 * matching press for. */
static bool seat_button_is_down(struct wlr_seat *seat, uint32_t button)
{
    for (size_t i = 0; i < seat->pointer_state.button_count; i++)
        if (seat->pointer_state.buttons[i].button == button &&
            seat->pointer_state.buttons[i].n_pressed > 0)
            return true;
    return false;
}

/* Button handling shared between real pointers and tablet-tool emulation. */
static void pointer_button(syn_server_t *s, uint32_t time_msec,
                           uint32_t button, enum wl_pointer_button_state state)
{
    notify_activity(s);

    /* Locked: a click wakes the native lock and goes no further — no window,
     * dock or panel underneath may be reached. */
    if (s->nlock.active) {
        lock_notify_activity(s);
        return;
    }

    /* A settings panel is up, so this button is not the desktop's. The RELEASE
     * matters as much as the press: the press was swallowed by the panel, and
     * forwarding only the release hands the focused client a release it has no
     * matching press for — which is how a client ends up thinking a button is
     * still down. Only the release of a press the seat actually saw is sent,
     * which is the same test the grab-release path below makes.
     *
     * Every in-flight drag is excluded first. A panel can be opened by a KEYBIND
     * in the middle of one (Super+P while dragging a window is a keystroke away),
     * and taking this branch then would eat the release that ends the grab — a
     * window welded to the cursor, with no way to put it down. Those releases
     * belong to the three branches below; this one only claims a button that
     * genuinely has nowhere else to go. */
    if (panel_pointer_active(s) &&
        state == WL_POINTER_BUTTON_STATE_RELEASED &&
        s->cursor_mode == SYNUI_CURSOR_PASSTHROUGH &&
        !s->dock_drag.active && !s->deskicon_drag.active) {
        if (seat_button_is_down(s->seat, button))
            wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);
        return;
    }

    /* A dock drag keeps cursor_mode == PASSTHROUGH, so catch its release
     * before the generic grab-release below. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->dock_drag.active) {
        dock_drag_end(s, s->cursor->x, s->cursor->y);
        return;
    }

    /* A desktop-icon drag is the same story: PASSTHROUGH throughout, so its
     * release has to be caught here or the drop would never be committed. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->deskicon_drag.active) {
        deskicon_drag_end(s, s->cursor->x, s->cursor->y);
        return;
    }

    /* A release always ends an in-progress grab. Ending a MOVE is where a drag
     * against a screen edge becomes a snap. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
        s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH) {
        /* An armed grab that never crossed the slop was a click, not a drag:
         * nothing moved, so there is no drop to resolve and no snap to apply. */
        if (s->cursor_mode == SYNUI_CURSOR_MOVE && !s->grab_armed)
            snap_drag_end(s, s->grabbed_view);
        s->grab_armed   = false;
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
        s->grabbed_view = NULL;

        /* The release is swallowed — unless the client began this grab itself.
         * A CSD app (Firefox dragging its own titlebar or edge) gets the press
         * forwarded, *then* asks for the grab with xdg_toplevel.move/.resize, so
         * the seat has that button down. Swallowing the release too left it down
         * forever, and pointer_update_focus honours the implicit grab it implies:
         * pointer focus froze on that client, so the cursor kept whatever image it
         * last set — the resize arrow — and never updated again no matter where
         * the pointer went. Deliver the release iff the seat still thinks the
         * button is down; a compositor-initiated grab (Super+drag, a border press)
         * never forwarded its press, so nothing is sent and the client sees no
         * stray release. */
        if (seat_button_is_down(s->seat, button))
            wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);

        /* Now that the grab is over, give the cursor back to the client under the
         * pointer — or re-derive it from the border it came to rest on — rather
         * than leaving the drag's arrow frozen on screen. */
        cursor_set_deco(s, NULL, time_msec);
        deco_hover_update(s, s->cursor->x, s->cursor->y, time_msec);
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        /* Click a toast to dismiss it. First, because toasts are drawn above
         * everything and sit over the top-right corner of whatever is there —
         * letting the click through to the window underneath would mean acting
         * on something you cannot see. Only swallows the click when one is
         * actually hit, so the corner is otherwise untouched. */
        if (button == BTN_LEFT && notif_click(s, s->cursor->x, s->cursor->y))
            return;

        /* The Bluetooth panel likewise: a click picks the device the keys then
         * act on, and a click off it puts the panel away. */
        if (s->bt.visible) {
            if (button == BTN_LEFT) bt_click(s, s->cursor->x, s->cursor->y);
            else                    bt_hide(s);
            return;
        }

        /* The command bar first: it holds the keyboard while it is up (see the
         * cmdbar branch at the top of keyboard_handle_key), so it outranks the
         * panels for the same reason it outranks them there. */
        if (cmdbar_click(s, s->cursor->x, s->cursor->y))
            return;

        /* The settings panels, in the order the keyboard chain walks them. One
         * of them being open means the click is theirs: on a row it does what
         * that row's key does, and off the panel it closes it — the
         * click-off-to-close every one of them went without until now. */
        if (panel_pointer_click(s, s->cursor->x, s->cursor->y, button, time_msec))
            return;

        /* The dock context menu is modal for the pointer while open: a left
         * click runs the item under the cursor, any other click dismisses. */
        if (s->dockmenu.visible) {
            if (button == BTN_LEFT)
                dockmenu_click(s, s->cursor->x, s->cursor->y);
            else
                dockmenu_close(s);
            return;
        }

        /* Same contract for the desktop context menu. */
        if (s->deskmenu.visible) {
            if (button == BTN_LEFT)
                deskmenu_click(s, s->cursor->x, s->cursor->y);
            else
                deskmenu_close(s);
            return;
        }

        /* No launcher hit-test here any more. The start button is a bar module
         * (quickshell/modules/Launcher.qml), so its clicks are the bar's like
         * every other module's. Hit-testing it in the compositor is what made
         * the bar's top-left corner invisible-but-clickable to the bar itself. */

        /* Right-click a dock icon → its context menu. */
        if (button == BTN_RIGHT) {
            syn_dock_entry_t *e =
                dock_entry_at(s, s->cursor->x, s->cursor->y);
            if (e) {
                dockmenu_open(s, e, s->cursor->x, s->cursor->y);
                return;
            }
        }

        if (button == BTN_LEFT) {
            syn_dock_entry_t *dock_hit =
                dock_entry_at(s, s->cursor->x, s->cursor->y);
            if (dock_hit) {
                dock_entry_click(s, dock_hit);
                return;
            }
            /* Press on the bar background (not an icon) begins a drag that,
             * on release, snaps the dock to the nearest screen edge. */
            if (dock_bar_at(s, s->cursor->x, s->cursor->y, NULL)) {
                dock_drag_begin(s, s->cursor->x, s->cursor->y);
                return;
            }
        }

        /* Server-side decorations claim the click before the client sees it:
         * the titlebar buttons, a titlebar drag (double-click = maximize), and
         * a border drag to resize from that edge. */
        if (button == BTN_LEFT) {
            syn_deco_region_t region;
            uint32_t edges;
            syn_view_t *dv = deco_at(s, s->cursor->x, s->cursor->y,
                                     &region, &edges);
            if (dv && region != DECO_NONE) {
                focus_view(s, dv, view_surface(dv));
                wlr_scene_node_raise_to_top(view_node(dv));

                switch (region) {
                case DECO_BTN_MIN:
                    view_apply_minimized(s, dv, 1);
                    break;
                case DECO_BTN_MAX:
                    view_apply_maximized(s, dv, !dv->maximized);
                    break;
                case DECO_BTN_CLOSE:
                    view_close(dv);
                    break;
                case DECO_BORDER:
                    begin_interactive_edges(dv, SYNUI_CURSOR_RESIZE, edges);
                    break;
                case DECO_TITLEBAR: {
                    bool dbl = (s->tb_last_click_view == dv) &&
                               (time_msec - s->tb_last_click_ms < 400);
                    s->tb_last_click_view = dbl ? NULL : dv;
                    s->tb_last_click_ms   = time_msec;
                    if (dbl)
                        view_apply_maximized(s, dv, !dv->maximized);
                    else
                        begin_interactive_armed(dv, SYNUI_CURSOR_MOVE, 0, true);
                    break;
                }
                default:
                    break;
                }
                return;
            }
        }

        double sx, sy;
        struct wlr_surface *surface = NULL;
        syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y,
                                    &surface, &sx, &sy);

        /* Nothing under the cursor means the wallpaper — the desktop itself.
         * The dock, panels and layer surfaces all returned above, so reaching
         * here with no view and no surface is the only "clicked the desktop"
         * signal there is. */
        if (!view && !surface) {
            int icon = deskicon_at(s, s->cursor->x, s->cursor->y);

            if (button == BTN_RIGHT) {
                deskicon_select(s, icon);   /* -1 clears */
                deskmenu_open(s, s->cursor->x, s->cursor->y);
                return;
            }
            if (button == BTN_LEFT) {
                deskicon_select(s, icon);
                if (icon >= 0) {
                    /* Single click selects, double click opens — the same
                     * 400ms window the titlebar uses. */
                    bool dbl = (s->deskicon_last_click_idx == icon) &&
                               (time_msec - s->deskicon_last_click_ms < 400);
                    s->deskicon_last_click_idx = dbl ? -1 : icon;
                    s->deskicon_last_click_ms  = time_msec;
                    if (dbl) {
                        deskicon_activate(s, icon);
                    } else {
                        /* Arm a move. It only becomes a drag once the cursor
                         * travels, so a plain single click still just selects. */
                        deskicon_drag_begin(s, icon, s->cursor->x, s->cursor->y);
                    }
                    return;
                }
                /* Clicking empty desktop drops focus decoration on the icons
                 * but is otherwise forwarded as normal. */
                s->deskicon_last_click_idx = -1;
            }
        }

        /* Is this click on one of the view's xdg_popups (a menu/tooltip)
         * rather than the toplevel itself? The popup's scene node lives in
         * the parent toplevel's scene_tree, so view_at resolves it to the
         * parent view — but a popup owns its own wlroots popup-grab and we
         * must NOT refocus/raise the parent on a popup click. */
        struct wlr_surface *root =
            surface ? wlr_surface_get_root_surface(surface) : NULL;
        struct wlr_xdg_popup *clicked_popup =
            root ? wlr_xdg_popup_try_from_wlr_surface(root) : NULL;
        bool is_popup = clicked_popup != NULL;

        /* This press opens an implicit grab if no other button is already
         * down. Record where the grab surface's origin sits relative to the
         * cursor now, while it still has pointer focus; pointer_update_focus
         * replays that offset to keep motion in its coordinate space for as
         * long as the grab lasts. */
        if (surface && s->seat->pointer_state.button_count == 0) {
            s->ptr_grab_off_x = s->cursor->x - sx;
            s->ptr_grab_off_y = s->cursor->y - sy;
        }

        /* Super + drag begins an interactive move/resize; the button is not
         * forwarded to the client. */
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
        uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
        if (view && !is_popup && (mods & WLR_MODIFIER_LOGO)) {
            if (button == BTN_LEFT) {
                begin_interactive(view, SYNUI_CURSOR_MOVE);
                return;
            }
            if (button == BTN_RIGHT) {
                begin_interactive(view, SYNUI_CURSOR_RESIZE);
                return;
            }
        }
        /* Skip focus_view for popup clicks: calling it re-sends a keyboard
         * enter to the popup surface, disrupting the popup-grab and making
         * GTK/XUL menus dismiss-without-activating. The working layer-shell
         * popup path skips focus_view too (its view_at returns NULL).
         *
         * For non-popup clicks route KEYBOARD focus to the view's *toplevel*
         * surface, not the raw surface under the cursor — that surface can be a
         * render-only subsurface (e.g. Firefox draws its noautohide permission
         * doorhanger as a subsurface of the toplevel). wl_keyboard.enter on a
         * subsurface leaves GTK/Firefox unable to route Tab/activation into the
         * panel (it never becomes the active modal), so clicks highlight but
         * don't fire and Tab skips it. Pointer delivery above stays per-surface
         * (hover highlight needs it); only keyboard focus follows the toplevel,
         * matching every other focus_view() call site. */
        if (view && !is_popup) focus_view(s, view, view_surface(view));
    }

    wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);
}

static void server_cursor_button(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_button);
    struct wlr_pointer_button_event *event = data;

    /* Super+click (move/resize a window) is Super used as a modifier, so it
     * must not also open the start menu when Super is finally released. */
    s->super_armed = 0;

    pointer_button(s, event->time_msec, event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    notify_activity(s);

    if (s->bt.visible &&
        event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        bt_scroll(s, event->delta);
        return;
    }

    /* An open settings panel takes the wheel, wherever the pointer is. Vertical
     * only: every one of these panels is a column, and a horizontal wheel (or a
     * touchpad's sideways flick) has nothing to mean in one. It is swallowed
     * rather than forwarded, because a modal panel must not scroll the window
     * underneath it. */
    if (panel_pointer_active(s)) {
        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
            panel_pointer_scroll(s, s->cursor->x, s->cursor->y, event->delta);
        return;
    }

    wlr_seat_pointer_notify_axis(s->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_frame);
    wlr_seat_pointer_notify_frame(s->seat);
}

static void server_request_cursor(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    if (event->seat_client == s->seat->pointer_state.focused_client)
        wlr_cursor_set_surface(s->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
}

/* ── Touch ───────────────────────────────────────────────── */
/* Touch is forwarded to the seat with per-point focus: a finger belongs to
 * the surface it landed on for its whole down→up arc. */
static void server_touch_down(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, touch_down);
    struct wlr_touch_down_event *event = data;
    notify_activity(s);

    double lx, ly, sx, sy;
    wlr_cursor_absolute_to_layout_coords(s->cursor, &event->touch->base,
                                         event->x, event->y, &lx, &ly);
    syn_view_t *view = NULL;
    struct wlr_surface *surface = surface_at(s, lx, ly, &view, &sx, &sy);
    if (!surface) return;

    wlr_seat_touch_notify_down(s->seat, surface, event->time_msec,
                               event->touch_id, sx, sy);
    if (view && !s->locked)
        focus_view(s, view, view_surface(view)); /* keyboard→toplevel, not a subsurface */
}

static void server_touch_motion(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, touch_motion);
    struct wlr_touch_motion_event *event = data;
    notify_activity(s);

    struct wlr_touch_point *point =
        wlr_seat_touch_get_point(s->seat, event->touch_id);
    if (!point) return;

    /* Surface-local coordinates are only meaningful against the surface the
     * finger went down on; skip motion once it slides off that surface. */
    double lx, ly, sx, sy;
    wlr_cursor_absolute_to_layout_coords(s->cursor, &event->touch->base,
                                         event->x, event->y, &lx, &ly);
    struct wlr_surface *surface = surface_at(s, lx, ly, NULL, &sx, &sy);
    if (surface && surface == point->surface)
        wlr_seat_touch_notify_motion(s->seat, event->time_msec,
                                     event->touch_id, sx, sy);
}

static void server_touch_up(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, touch_up);
    struct wlr_touch_up_event *event = data;
    notify_activity(s);
    if (wlr_seat_touch_get_point(s->seat, event->touch_id))
        wlr_seat_touch_notify_up(s->seat, event->time_msec, event->touch_id);
}

static void server_touch_cancel(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, touch_cancel);
    struct wlr_touch_cancel_event *event = data;
    struct wlr_touch_point *point =
        wlr_seat_touch_get_point(s->seat, event->touch_id);
    if (point && point->client)
        wlr_seat_touch_notify_cancel(s->seat, point->client);
}

static void server_touch_frame(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, touch_frame);
    wlr_seat_touch_notify_frame(s->seat);
}

/* ── Tablet (pointer emulation) ──────────────────────────── */
/* Tablet tools drive the regular cursor: pen motion moves the pointer, the
 * tip is the left button, the stylus barrel buttons map to right/middle.
 * Full tablet-v2 (pressure/tilt for drawing apps) is a Phase H+ follow-up. */
static void tablet_warp_cursor(syn_server_t *s, struct wlr_tablet *tablet,
                               double x, double y, uint32_t time_msec)
{
    wlr_cursor_warp_absolute(s->cursor, &tablet->base, x, y);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;
    wlr_scene_node_set_position(&s->drag_icon_tree->node,
                                (int)s->cursor->x, (int)s->cursor->y);
    notify_activity(s);

    if (s->cursor_mode == SYNUI_CURSOR_MOVE)   { process_cursor_move(s);   return; }
    if (s->cursor_mode == SYNUI_CURSOR_RESIZE) { process_cursor_resize(s); return; }
    pointer_update_focus(s, time_msec);
}

static void server_tablet_axis(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, tablet_axis);
    struct wlr_tablet_tool_axis_event *event = data;
    /* NAN = keep the cursor's current value on that axis. */
    double x = (event->updated_axes & WLR_TABLET_TOOL_AXIS_X) ? event->x : NAN;
    double y = (event->updated_axes & WLR_TABLET_TOOL_AXIS_Y) ? event->y : NAN;
    if (isnan(x) && isnan(y)) return;
    tablet_warp_cursor(s, event->tablet, x, y, event->time_msec);
}

static void server_tablet_proximity(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, tablet_proximity);
    struct wlr_tablet_tool_proximity_event *event = data;
    if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN)
        tablet_warp_cursor(s, event->tablet, event->x, event->y,
                           event->time_msec);
}

static void server_tablet_tip(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, tablet_tip);
    struct wlr_tablet_tool_tip_event *event = data;
    pointer_button(s, event->time_msec, BTN_LEFT,
                   event->state == WLR_TABLET_TOOL_TIP_DOWN
                       ? WL_POINTER_BUTTON_STATE_PRESSED
                       : WL_POINTER_BUTTON_STATE_RELEASED);
    wlr_seat_pointer_notify_frame(s->seat);
}

static void server_tablet_button(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, tablet_button);
    struct wlr_tablet_tool_button_event *event = data;
    uint32_t button = (event->button == BTN_STYLUS2) ? BTN_MIDDLE : BTN_RIGHT;
    pointer_button(s, event->time_msec, button,
                   event->state == WLR_BUTTON_PRESSED
                       ? WL_POINTER_BUTTON_STATE_PRESSED
                       : WL_POINTER_BUTTON_STATE_RELEASED);
    wlr_seat_pointer_notify_frame(s->seat);
}

/* ── Touchpad gestures (pointer-gestures-v1 relay) ───────── */
static void server_swipe_begin(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;
    notify_activity(s);
    wlr_pointer_gestures_v1_send_swipe_begin(s->pointer_gestures, s->seat,
                                             event->time_msec, event->fingers);
}

static void server_swipe_update(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;
    wlr_pointer_gestures_v1_send_swipe_update(s->pointer_gestures, s->seat,
                                              event->time_msec,
                                              event->dx, event->dy);
}

static void server_swipe_end(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;
    wlr_pointer_gestures_v1_send_swipe_end(s->pointer_gestures, s->seat,
                                           event->time_msec, event->cancelled);
}

static void server_pinch_begin(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    notify_activity(s);
    wlr_pointer_gestures_v1_send_pinch_begin(s->pointer_gestures, s->seat,
                                             event->time_msec, event->fingers);
}

static void server_pinch_update(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;
    wlr_pointer_gestures_v1_send_pinch_update(s->pointer_gestures, s->seat,
                                              event->time_msec,
                                              event->dx, event->dy,
                                              event->scale, event->rotation);
}

static void server_pinch_end(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;
    wlr_pointer_gestures_v1_send_pinch_end(s->pointer_gestures, s->seat,
                                           event->time_msec, event->cancelled);
}

static void server_hold_begin(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    notify_activity(s);
    wlr_pointer_gestures_v1_send_hold_begin(s->pointer_gestures, s->seat,
                                            event->time_msec, event->fingers);
}

static void server_hold_end(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, hold_end);
    struct wlr_pointer_hold_end_event *event = data;
    wlr_pointer_gestures_v1_send_hold_end(s->pointer_gestures, s->seat,
                                          event->time_msec, event->cancelled);
}

/* ── New input device ────────────────────────────────────── */
/* Non-keyboard devices are tracked in s->input_devs so a SIGHUP config
 * reload can revisit them with new libinput options. */
static void input_dev_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_input_dev_t *id = wl_container_of(listener, id, destroy);
    wl_list_remove(&id->destroy.link);
    /* Initialised for every device, attached only for switches — so removing
     * it is safe either way. */
    wl_list_remove(&id->toggle.link);
    wl_list_remove(&id->link);
    free(id);
}

static syn_input_dev_t *input_dev_track(syn_server_t *s,
                                        struct wlr_input_device *dev)
{
    syn_input_dev_t *id = calloc(1, sizeof(*id));
    if (!id) return NULL;
    id->server = s;
    id->dev = dev;
    id->destroy.notify = input_dev_handle_destroy;
    wl_signal_add(&dev->events.destroy, &id->destroy);
    /* Only switch devices attach this one; init it either way so the destroy
     * handler can remove it unconditionally. */
    wl_list_init(&id->toggle.link);
    wl_list_insert(&s->input_devs, &id->link);
    return id;
}

/* A switch flipped. libinput reports the lid as WLR_SWITCH_TYPE_LID with state
 * ON meaning *closed* — the switch is "lid switch closed", not "lid open" —
 * which is the one thing here worth getting the wrong way round. */
static void switch_handle_toggle(struct wl_listener *listener, void *data)
{
    syn_input_dev_t *id = wl_container_of(listener, id, toggle);
    struct wlr_switch_toggle_event *ev = data;

    if (ev->switch_type != WLR_SWITCH_TYPE_LID) return;   /* tablet mode, etc. */
    power_lid_set(id->server, ev->switch_state == WLR_SWITCH_STATE_ON);
}

/* Switches are not a seat capability and have no cursor to attach to, so they
 * get tracked for the destroy bookkeeping and nothing else. */
static void server_new_switch(syn_server_t *s, struct wlr_input_device *dev)
{
    syn_input_dev_t *id = input_dev_track(s, dev);
    if (!id) return;

    id->toggle.notify = switch_handle_toggle;
    wl_signal_add(&wlr_switch_from_input_device(dev)->events.toggle, &id->toggle);

    /* Whether this switch is a *lid*, asked here rather than inferred from the
     * first toggle event. A laptop whose lid has not moved since login emits
     * no toggle at all, so waiting for one would have the power panel report
     * "no lid switch" on a machine that plainly has one — the lid rows would
     * read as dead on exactly the hardware they exist for. A tablet-mode-only
     * switch must still not claim to be a lid, which is why this asks libinput
     * instead of assuming any switch is one. */
    bool is_lid = false;
    if (wlr_input_device_is_libinput(dev)) {
        struct libinput_device *li = wlr_libinput_get_device_handle(dev);
        is_lid = li &&
                 libinput_device_switch_has_switch(li, LIBINPUT_SWITCH_LID) > 0;
        if (is_lid) s->power.lid_seen = 1;
    }

    wlr_log(WLR_INFO, "synui: input: switch device '%s'%s", dev->name,
            is_lid ? " (lid)" : "");
}

static void server_new_input(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_input);
    struct wlr_input_device *dev = data;

    switch (dev->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(s, dev);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        s->touch_devices++;
        /* fallthrough */
    case WLR_INPUT_DEVICE_POINTER:
    case WLR_INPUT_DEVICE_TABLET:
        input_apply_libinput_config(s, dev);
        input_dev_track(s, dev);
        wlr_cursor_attach_input_device(s->cursor, dev);
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        server_new_switch(s, dev);
        break;
    default:
        break;
    }

    seat_update_capabilities(s);
}

static void server_request_set_selection(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(s->seat, event->source, event->serial);
}

static void server_request_set_primary_selection(struct wl_listener *listener,
                                                 void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(s->seat, event->source, event->serial);
}

/* ── Drag-and-drop ───────────────────────────────────────── */
/* When the drag (and its implicit grab) ends, the surface under the cursor
 * regains normal pointer focus. */
static void server_drag_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, drag_destroy);
    wl_list_remove(&s->drag_destroy.link);
    wl_list_init(&s->drag_destroy.link);
    if (!s->shutting_down)
        pointer_update_focus(s, 0);
}

static void server_request_start_drag(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;

    if (wlr_seat_validate_pointer_grab_serial(s->seat, event->origin,
                                              event->serial)) {
        wlr_seat_start_pointer_drag(s->seat, event->drag, event->serial);
        return;
    }
    struct wlr_touch_point *point;
    if (wlr_seat_validate_touch_grab_serial(s->seat, event->origin,
                                            event->serial, &point)) {
        wlr_seat_start_touch_drag(s->seat, event->drag, event->serial, point);
        return;
    }
    wlr_data_source_destroy(event->drag->source);
}

static void server_start_drag(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, start_drag);
    struct wlr_drag *drag = data;

    /* Only one drag per seat; re-arm the destroy listener for it. */
    wl_list_remove(&s->drag_destroy.link);
    s->drag_destroy.notify = server_drag_destroy;
    wl_signal_add(&drag->events.destroy, &s->drag_destroy);

    if (drag->icon) {
        wlr_scene_drag_icon_create(s->drag_icon_tree, drag->icon);
        wlr_scene_node_set_position(&s->drag_icon_tree->node,
                                    (int)s->cursor->x, (int)s->cursor->y);
    }
}

/* Reapply the input side of a reloaded config: keymap + repeat to every
 * keyboard, libinput options to every tracked pointer/touch/tablet. */
void input_reload_config(syn_server_t *s)
{
    syn_keyboard_t *kb;
    wl_list_for_each(kb, &s->keyboards, link)
        keyboard_apply_config(s, kb->wlr_keyboard);

    syn_input_dev_t *id;
    wl_list_for_each(id, &s->input_devs, link)
        input_apply_libinput_config(s, id->dev);
}

/* ── Setup all input listeners ───────────────────────────── */
void input_setup(syn_server_t *s)
{
    wl_list_init(&s->input_devs);

    /* The compositor always drives a cursor, so advertise the pointer
     * capability up front — before this, a seat with no input devices yet
     * (headless, or early clients racing the backend) had no capabilities
     * and a client calling get_pointer was killed with a protocol error. */
    wlr_seat_set_capabilities(s->seat, WL_SEAT_CAPABILITY_POINTER);

    s->new_input.notify = server_new_input;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);

    s->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);

    s->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);

    s->cursor_button.notify = server_cursor_button;
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);

    s->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);

    s->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&s->cursor->events.frame, &s->cursor_frame);

    s->request_cursor.notify = server_request_cursor;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_cursor);

    s->request_set_selection.notify = server_request_set_selection;
    wl_signal_add(&s->seat->events.request_set_selection,
                   &s->request_set_selection);

    s->request_set_primary_selection.notify = server_request_set_primary_selection;
    wl_signal_add(&s->seat->events.request_set_primary_selection,
                   &s->request_set_primary_selection);

    /* Drag-and-drop: validate + start the grab, show the icon at the cursor.
     * drag_destroy is armed per drag; init it so removal is always safe. */
    s->request_start_drag.notify = server_request_start_drag;
    wl_signal_add(&s->seat->events.request_start_drag, &s->request_start_drag);
    s->start_drag.notify = server_start_drag;
    wl_signal_add(&s->seat->events.start_drag, &s->start_drag);
    wl_list_init(&s->drag_destroy.link);

    /* Touch (wlr_cursor maps it into the output layout for us). */
    s->touch_down.notify = server_touch_down;
    wl_signal_add(&s->cursor->events.touch_down, &s->touch_down);
    s->touch_up.notify = server_touch_up;
    wl_signal_add(&s->cursor->events.touch_up, &s->touch_up);
    s->touch_motion.notify = server_touch_motion;
    wl_signal_add(&s->cursor->events.touch_motion, &s->touch_motion);
    s->touch_cancel.notify = server_touch_cancel;
    wl_signal_add(&s->cursor->events.touch_cancel, &s->touch_cancel);
    s->touch_frame.notify = server_touch_frame;
    wl_signal_add(&s->cursor->events.touch_frame, &s->touch_frame);

    /* Tablet tools (pointer emulation). */
    s->tablet_axis.notify = server_tablet_axis;
    wl_signal_add(&s->cursor->events.tablet_tool_axis, &s->tablet_axis);
    s->tablet_proximity.notify = server_tablet_proximity;
    wl_signal_add(&s->cursor->events.tablet_tool_proximity, &s->tablet_proximity);
    s->tablet_tip.notify = server_tablet_tip;
    wl_signal_add(&s->cursor->events.tablet_tool_tip, &s->tablet_tip);
    s->tablet_button.notify = server_tablet_button;
    wl_signal_add(&s->cursor->events.tablet_tool_button, &s->tablet_button);

    /* Touchpad gestures → pointer-gestures-v1 clients. */
    s->pointer_gestures = wlr_pointer_gestures_v1_create(s->display);
    s->swipe_begin.notify = server_swipe_begin;
    wl_signal_add(&s->cursor->events.swipe_begin, &s->swipe_begin);
    s->swipe_update.notify = server_swipe_update;
    wl_signal_add(&s->cursor->events.swipe_update, &s->swipe_update);
    s->swipe_end.notify = server_swipe_end;
    wl_signal_add(&s->cursor->events.swipe_end, &s->swipe_end);
    s->pinch_begin.notify = server_pinch_begin;
    wl_signal_add(&s->cursor->events.pinch_begin, &s->pinch_begin);
    s->pinch_update.notify = server_pinch_update;
    wl_signal_add(&s->cursor->events.pinch_update, &s->pinch_update);
    s->pinch_end.notify = server_pinch_end;
    wl_signal_add(&s->cursor->events.pinch_end, &s->pinch_end);
    s->hold_begin.notify = server_hold_begin;
    wl_signal_add(&s->cursor->events.hold_begin, &s->hold_begin);
    s->hold_end.notify = server_hold_end;
    wl_signal_add(&s->cursor->events.hold_end, &s->hold_end);

    /* pointer-constraints + relative-pointer (constraints.c). */
    constraints_setup(s);

    /* virtual-keyboard-v1 (wtype support — see server_new_virtual_keyboard). */
    s->virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(s->display);
    s->new_virtual_keyboard.notify = server_new_virtual_keyboard;
    wl_signal_add(&s->virtual_keyboard_mgr->events.new_virtual_keyboard,
                 &s->new_virtual_keyboard);
    s->vkb_mgr_destroy.notify = server_vkb_mgr_destroy;
    wl_signal_add(&s->virtual_keyboard_mgr->events.destroy,
                 &s->vkb_mgr_destroy);
}
