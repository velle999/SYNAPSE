/*
 * input.c — Keyboard, pointer, touch and tablet handling
 *
 * Keybindings are table-driven (config.c seeds the defaults; `bind =` lines
 * in synuirc add or override). Default table:
 *
 *   Super+Enter          Launch terminal (foot)
 *   Super+Space          Open AI command bar
 *   Super+A              Toggle neural overlay
 *   Super+D              Toggle display settings (rotate/arrange monitors)
 *   Super+Escape         Toggle the welcome menu
 *   Super+Q              Close focused window
 *   Super+Shift+Q        Quit synui
 *   Super+Tab            Cycle layout (tile → floating → monocle → AI → tile)
 *   Super+J/K            Focus next/prev window
 *   Super+Shift+J/K      Move window down/up the stack
 *   Super+H/Shift+L      Shrink / grow the master column
 *   Super+L              Lock the screen (power_lock_cmd / swaylock)
 *   Super+F              Toggle floating (centred placement)
 *   Super+M              Toggle maximize
 *   Super+N              Minimize focused window
 *   Super+Shift+N        Restore a minimized window on this workspace
 *   Super+1..9           Switch to workspace N
 *   Super+Shift+1..9     Move focused window to workspace N
 *   Super+Backspace      Command bar, scoped to the focused window (ai_ask)
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
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-names.h>
#include <wlr/backend/libinput.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
/* wlr_keyboard_notify_modifiers() — pushing synthetic modifier state (the
 * NumLock lock below) is only declared on the backend-facing interface. */
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_primary_selection.h>
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
        return;
    }

    syn_view_t *prev = s->focused_view;
    s->focused_view = view;

    /* L3: brief chromatic-aberration pulse on an actual focus change. */
    if (prev != view)
        effects_notify_focus(s);

    /* Raise to top of scene */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    /* Toggle activated state (X11 clients need this to accept input) and
     * refresh border colours. */
    if (prev && prev != view) {
        view_set_activated(prev, 0);
        view_update_borders(prev);
        foreign_toplevel_update_state(prev);
    }
    view_set_activated(view, 1);
    view_update_borders(view);
    foreign_toplevel_update_state(view);

    /* Notify seat */
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(s->seat, surface,
                                        kb->keycodes, kb->num_keycodes,
                                        &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(s->seat, surface, NULL, 0, NULL);
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
void view_set_security(syn_view_t *view, win_security_t state)
{
    view->security = state;
    view_update_borders(view);
}

void view_update_borders(syn_view_t *view)
{
    if (!view->mapped) return;
    /* No sane geometry yet (e.g. focused at map before the first layout) — the
     * borders will be (re)created once the window is sized. */
    if (view->w <= 0 || view->h <= 0) return;

    /* Fullscreen covers the whole output; borders would poke out past it. */
    if (view->fullscreen) {
        if (view->border_top)    wlr_scene_node_set_enabled(&view->border_top->node,    false);
        if (view->border_bottom) wlr_scene_node_set_enabled(&view->border_bottom->node, false);
        if (view->border_left)   wlr_scene_node_set_enabled(&view->border_left->node,   false);
        if (view->border_right)  wlr_scene_node_set_enabled(&view->border_right->node,  false);
        return;
    }

    /* Pick border color (config-driven; defaults COLOR_BORDER_*) */
    float color[4];
    syn_config_t *cfg = &view->server->config;

    if (view->security == WIN_SECURE_ALERT ||
        view->security == WIN_SECURE_DENIED)
        memcpy(color, cfg->border_color_warn, sizeof(color));
    else if (view->ai_ctx.has_ctx)
        memcpy(color, cfg->border_color_ai, sizeof(color));
    else if (view == view->server->focused_view)
        memcpy(color, cfg->border_color_focus, sizeof(color));
    else
        memcpy(color, cfg->border_color_norm, sizeof(color));

    int x = view->x, y = view->y, w = view->w, h = view->h;
    int bw = view->server->config.border_width;
    int side_h = h - 2 * bw;      /* side borders sit between top/bottom */
    if (side_h < 0) side_h = 0;   /* scene rects must be non-negative */

    /* Create borders as scene rects if they don't exist yet. Existing rects
     * are re-sized too: the view may have been resized (retile), and a config
     * reload can change border_width. */
    #define MAKE_BORDER(field, bx, by, bw2, bh) do { \
        if (!view->field) \
            view->field = wlr_scene_rect_create(view->scene_tree->node.parent, \
                                                bw2, bh, color); \
        else { \
            wlr_scene_rect_set_color(view->field, color); \
            wlr_scene_rect_set_size(view->field, bw2, bh); \
        } \
        wlr_scene_node_set_enabled(&view->field->node, true); \
        wlr_scene_node_set_position(&view->field->node, bx, by); \
    } while(0)

    MAKE_BORDER(border_top,    x,        y,        w,  bw);
    MAKE_BORDER(border_bottom, x,        y+h-bw,   w,  bw);
    MAKE_BORDER(border_left,   x,        y+bw,     bw, side_h);
    MAKE_BORDER(border_right,  x+w-bw,   y+bw,     bw, side_h);

    #undef MAKE_BORDER
}

/* ── Keyboard ────────────────────────────────────────────── */
static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    syn_keyboard_t *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat,
                                        &kb->wlr_keyboard->modifiers);
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

/* ── Start menu (Super-tap) ──────────────────────────────── */

/* Where in the bar the start button is. waybar's "◢ SYNAPSE" module is the sole
 * entry in modules-left, so it sits hard against the bar's left edge and a click
 * a couple of dozen pixels in lands on it. This is the one brittle assumption
 * here: reorder modules-left and the tap opens whatever moved into that corner.
 * waybar exposes no geometry (and no IPC to open a menu), so there is nothing
 * better to key off than position. */
#define START_MENU_HIT_X  24

/* The bar that owns the start menu. Matched on the layer-shell namespace rather
 * than "the first top-layer surface", so an unrelated panel can't be clicked. */
static syn_layer_surface_t *bar_on(syn_output_t *o)
{
    syn_layer_surface_t *ls;
    wl_list_for_each(ls, &o->layer_surfaces, link) {
        struct wlr_layer_surface_v1 *lsurf = ls->layer_surface;
        if (!lsurf || !lsurf->surface || !lsurf->surface->mapped) continue;
        if (lsurf->namespace && strcmp(lsurf->namespace, "waybar") == 0)
            return ls;
    }
    return NULL;
}

static syn_layer_surface_t *find_bar(syn_server_t *s)
{
    /* The bar on the output you are looking at, if it has one; otherwise any
     * bar at all, so the tap still works from a monitor that carries none. */
    syn_output_t *f = server_focused_output(s);
    if (f) {
        syn_layer_surface_t *ls = bar_on(f);
        if (ls) return ls;
    }

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        syn_layer_surface_t *ls = bar_on(o);
        if (ls) return ls;
    }
    return NULL;
}

static uint32_t now_msec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Open waybar's start menu.
 *
 * waybar's menu is a GTK popup that opens on a click on the module's widget:
 * there is no IPC, no signal and no protocol to open it from outside. So synui
 * does what a user does — it clicks the button, by sending the bar surface a
 * pointer enter/press/release through the seat. The physical cursor is left
 * where it is; only the seat's pointer focus moves, and the next real pointer
 * motion puts that back.
 *
 * Nothing here is load-bearing: if the bar isn't running, or waybar changes its
 * layout, the click misses and nothing happens. That is the intended failure —
 * a tap that does nothing, not a compositor that breaks. */
void synui_start_menu_open(syn_server_t *s)
{
    syn_layer_surface_t *bar = find_bar(s);
    if (!bar) {
        wlr_log(WLR_DEBUG, "synui: start menu: no waybar surface to click");
        return;
    }

    struct wlr_surface *surf = bar->layer_surface->surface;
    double sx = START_MENU_HIT_X;
    double sy = surf->current.height / 2.0;

    /* Clicking outside the surface would be silently ignored by the client, so
     * a bar too narrow for the hit point is worth a word rather than a mystery. */
    if (sx >= surf->current.width || surf->current.height <= 0) {
        wlr_log(WLR_DEBUG, "synui: start menu: bar is %dx%d, hit point outside it",
                surf->current.width, surf->current.height);
        return;
    }

    uint32_t t = now_msec();
    wlr_seat_pointer_notify_enter(s->seat, surf, sx, sy);
    wlr_seat_pointer_notify_motion(s->seat, t, sx, sy);
    wlr_seat_pointer_notify_button(s->seat, t, BTN_LEFT,
                                   WL_POINTER_BUTTON_STATE_PRESSED);
    wlr_seat_pointer_notify_frame(s->seat);
    wlr_seat_pointer_notify_button(s->seat, now_msec(), BTN_LEFT,
                                   WL_POINTER_BUTTON_STATE_RELEASED);
    wlr_seat_pointer_notify_frame(s->seat);
}

/* Execute a bind action (see config.c for the names and defaults). */
void synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    syn_workspace_t *ws = server_active_workspace(s);

    if (strcmp(action, "spawn") == 0) {
        spawn(arg);
    } else if (strcmp(action, "term") == 0) {
        /* Default config: fall back through common terminals. */
        if (strcmp(s->config.terminal, "foot") == 0)
            spawn("foot || alacritty || xterm");
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
    } else if (strcmp(action, "close") == 0) {
        if (s->focused_view) view_close(s->focused_view);
    } else if (strcmp(action, "quit") == 0) {
        wl_display_terminate(s->display);
    } else if (strcmp(action, "layout_cycle") == 0) {
        ws->layout = (ws->layout + 1) % 4;
        static const char *lnames[] = {"tiling","floating","monocle","AI"};
        wlr_log(WLR_INFO, "synui: layout → %s", lnames[ws->layout]);
        layout_apply(s, ws);
    } else if (strcmp(action, "master_shrink") == 0) {
        layout_adjust_master(s, ws, -0.05f);
    } else if (strcmp(action, "master_grow") == 0) {
        layout_adjust_master(s, ws, +0.05f);
    } else if (strcmp(action, "focus_next") == 0) {
        focus_next(s, 1);
    } else if (strcmp(action, "focus_prev") == 0) {
        focus_next(s, -1);
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
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
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
        s->focused_view->maximized = !s->focused_view->maximized;
        view_set_maximized(s->focused_view, s->focused_view->maximized);
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
    } else if (strcmp(action, "power") == 0) {
        power_toggle(s);
    } else if (strcmp(action, "taskmgr") == 0) {
        taskmgr_toggle(s);
    } else if (strcmp(action, "game") == 0) {
        game_toggle(s);
    } else if (strcmp(action, "cat") == 0) {
        cat_toggle(s);
    } else if (strcmp(action, "lock") == 0) {
        /* Same swaylock the idle timer and the power panel's Lock row run;
         * power_lock_cmd already guards against stacking a second instance. */
        spawn(s->config.power_lock_cmd);
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
        ctlpanel_toggle(s);
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
         * cyclic); arg "prev" goes the other way. Tiled/monocle/maximized
         * windows are repositioned by the target output's layout via
         * workspace_move_view; floating and fullscreen windows carry their own
         * absolute geometry, so translate or re-cover them onto it explicitly. */
        syn_view_t *v = s->focused_view;
        if (!v || !v->mapped) return;
        syn_output_t *cur = (v->workspace && v->workspace->output)
                                ? v->workspace->output
                                : server_focused_output(s);
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

        workspace_move_view(s, v, next->active_workspace);

        if (v->fullscreen) {
            v->x = to.x;      v->y = to.y;
            v->w = to.width;  v->h = to.height;
            if (v->is_xwayland)
                wlr_xwayland_surface_configure(v->xsurface, to.x, to.y,
                                               to.width, to.height);
            else
                wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel,
                                          to.width, to.height);
            wlr_scene_node_set_position(&v->scene_tree->node, to.x, to.y);
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
        } else if (v->floating) {
            /* Keep the same on-screen position relative to the monitor. */
            int nx = v->x + (to.x - from.x);
            int ny = v->y + (to.y - from.y);
            if (nx + v->w > to.x + to.width)  nx = to.x + to.width  - v->w;
            if (ny + v->h > to.y + to.height) ny = to.y + to.height - v->h;
            if (nx < to.x) nx = to.x;
            if (ny < to.y) ny = to.y;
            view_resize(v, nx, ny, v->w, v->h);
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
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

    xkb_keysym_t lower = xkb_keysym_to_lower(sym);

    for (int i = 0; i < s->config.bind_count; i++) {
        syn_bind_t *b = &s->config.binds[i];
        if (b->mods == mods && b->sym == lower) {
            synui_binding_execute(s, b->action, b->arg);
            return true;
        }
    }
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

    /* While the session is locked, compositor bindings are disabled and keys
     * go straight to the lock surface (which holds keyboard focus). */
    if (s->locked) {
        wlr_seat_set_keyboard(s->seat, wlr_kb);
        wlr_seat_keyboard_notify_key(s->seat, event->time_msec,
                                      event->keycode, event->state);
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

        /* CRT filter panel: same modal contract as the power panel. */
        for (int i = 0; i < nsyms; i++)
            if (filters_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Control panel: same modal contract again. */
        for (int i = 0; i < nsyms; i++)
            if (ctlpanel_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

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
        wlr_seat_set_keyboard(s->seat, wlr_kb);
        wlr_seat_keyboard_notify_key(s->seat, event->time_msec,
                                      event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
    syn_keyboard_t *kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
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
static void begin_interactive(syn_view_t *view, syn_cursor_mode_t mode)
{
    syn_server_t *s = view->server;
    if (!view->mapped || view->fullscreen) return;

    if (!view->floating) {
        view->floating = 1;
        layout_apply(s, view->workspace);   /* reflow remaining tiled windows */
    }
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
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
        uint32_t edges = 0;
        edges |= (s->cursor->x < view->x + view->w / 2)
                     ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
        edges |= (s->cursor->y < view->y + view->h / 2)
                     ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
        s->resize_edges = edges;
    }
}

static void process_cursor_move(syn_server_t *s)
{
    syn_view_t *v = s->grabbed_view;
    v->x = (int)(s->cursor->x - s->grab_x);
    v->y = (int)(s->cursor->y - s->grab_y);
    wlr_scene_node_set_position(&v->scene_tree->node, v->x, v->y);
    view_update_borders(v);
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
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
        wlr_seat_pointer_notify_clear_focus(s->seat);
    }
    constraints_focus_surface(s, surface);
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
    /* Context menu hover highlight follows the cursor. */
    if (s->dockmenu.visible)
        dockmenu_motion(s, s->cursor->x, s->cursor->y);

    /* Let the auto-hide dock react to the cursor reaching its edge. */
    dock_pointer_motion(s);

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

/* Button handling shared between real pointers and tablet-tool emulation. */
static void pointer_button(syn_server_t *s, uint32_t time_msec,
                           uint32_t button, enum wl_pointer_button_state state)
{
    notify_activity(s);

    /* A dock drag keeps cursor_mode == PASSTHROUGH, so catch its release
     * before the generic grab-release below. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->dock_drag.active) {
        dock_drag_end(s, s->cursor->x, s->cursor->y);
        return;
    }

    /* A release always ends an in-progress grab and is swallowed. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
        s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH) {
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
        s->grabbed_view = NULL;
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        /* The dock context menu is modal for the pointer while open: a left
         * click runs the item under the cursor, any other click dismisses. */
        if (s->dockmenu.visible) {
            if (button == BTN_LEFT)
                dockmenu_click(s, s->cursor->x, s->cursor->y);
            else
                dockmenu_close(s);
            return;
        }

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

        double sx, sy;
        struct wlr_surface *surface = NULL;
        syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y,
                                    &surface, &sx, &sy);

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
    wl_list_remove(&id->link);
    free(id);
}

static void input_dev_track(syn_server_t *s, struct wlr_input_device *dev)
{
    syn_input_dev_t *id = calloc(1, sizeof(*id));
    if (!id) return;
    id->dev = dev;
    id->destroy.notify = input_dev_handle_destroy;
    wl_signal_add(&dev->events.destroy, &id->destroy);
    wl_list_insert(&s->input_devs, &id->link);
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
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&s->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (s->touch_devices > 0)
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(s->seat, caps);
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
