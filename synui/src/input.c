/*
 * input.c — Keyboard and pointer handling
 *
 * Keyboard bindings:
 *
 *   Super+Enter          Launch terminal (foot)
 *   Super+Space          Open AI command bar
 *   Super+A              Toggle neural overlay
 *   Super+Q              Close focused window
 *   Super+Shift+Q        Quit synui
 *   Super+Tab            Cycle layout (tile → floating → monocle → AI → tile)
 *   Super+J/K            Focus next/prev window
 *   Super+Shift+J/K      Move window down/up the stack
 *   Super+H/L            Shrink / grow the master column
 *   Super+F              Toggle floating (centred placement)
 *   Super+M              Toggle maximize
 *   Super+1..9           Switch to workspace N
 *   Super+Shift+1..9     Move focused window to workspace N
 *   Super+Backspace      Spawn: syn ask (quick AI query)
 *
 * Pointer (interactive floating window management):
 *   Super + Left-drag    Move the window under the cursor
 *   Super + Right-drag   Resize it from the nearest corner
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_idle_notify_v1.h>

#include "synui.h"

/* Report user activity to idle-notify clients (swayidle). */
static inline void notify_activity(syn_server_t *s)
{
    if (s->idle_notifier)
        wlr_idle_notifier_v1_notify_activity(s->idle_notifier, s->seat);
}

/* ── Focus ───────────────────────────────────────────────── */
void focus_view(syn_server_t *s, syn_view_t *view, struct wlr_surface *surface)
{
    if (!s) return;
    if (!view) {
        wlr_seat_keyboard_notify_clear_focus(s->seat);
        return;
    }

    syn_view_t *prev = s->focused_view;
    s->focused_view = view;

    /* Raise to top of scene */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    /* Toggle activated state (X11 clients need this to accept input) and
     * refresh border colours. */
    if (prev && prev != view) {
        view_set_activated(prev, 0);
        view_update_borders(prev);
    }
    view_set_activated(view, 1);
    view_update_borders(view);

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

    /* Pick border color */
    float color[4];

    if (view->security == WIN_SECURE_ALERT ||
        view->security == WIN_SECURE_DENIED) {
        float c[] = COLOR_BORDER_WARN;
        memcpy(color, c, sizeof(color));
    } else if (view->ai_ctx.has_ctx) {
        float c[] = COLOR_BORDER_AI;
        memcpy(color, c, sizeof(color));
    } else if (view->server && view == view->server->focused_view) {
        float c[] = COLOR_BORDER_FOCUS;
        memcpy(color, c, sizeof(color));
    } else {
        float c[] = COLOR_BORDER_NORM;
        memcpy(color, c, sizeof(color));
    }

    int x = view->x, y = view->y, w = view->w, h = view->h;
    int bw = BORDER_WIDTH;
    int side_h = h - 2 * bw;      /* side borders sit between top/bottom */
    if (side_h < 0) side_h = 0;   /* scene rects must be non-negative */

    /* Create borders as scene rects if they don't exist yet */
    #define MAKE_BORDER(field, bx, by, bw2, bh) do { \
        if (!view->field) \
            view->field = wlr_scene_rect_create(view->scene_tree->node.parent, \
                                                bw2, bh, color); \
        else \
            wlr_scene_rect_set_color(view->field, color); \
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
    syn_workspace_t *ws = &s->workspaces[s->active_workspace];
    if (wl_list_empty(&ws->windows)) return;

    struct wl_list *target;
    if (!s->focused_view) {
        target = ws->windows.next;
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

static bool handle_keybinding(syn_server_t *s, xkb_keysym_t sym,
                               uint32_t modifiers)
{
    bool super = (modifiers & WLR_MODIFIER_LOGO) != 0;
    bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;

    if (!super) return false;

    /* Normalize shifted keysyms to lowercase for consistent matching */
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);

    /* Super+Shift+Q — quit */
    if (shift && lower == XKB_KEY_q) {
        wl_display_terminate(s->display);
        return true;
    }

    /* Super+Q — close focused window */
    if (!shift && lower == XKB_KEY_q) {
        if (s->focused_view)
            view_close(s->focused_view);
        return true;
    }

    /* Super+Enter — launch terminal */
    if (sym == XKB_KEY_Return) {
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c", "foot || alacritty || xterm", NULL);
            _exit(1);
        }
        return true;
    }

    /* Super+Space — AI command bar */
    if (sym == XKB_KEY_space) {
        if (s->cmdbar.visible)
            cmdbar_hide(s);
        else
            cmdbar_show(s);
        return true;
    }

    /* Super+A — neural overlay */
    if (!shift && lower == XKB_KEY_a) {
        overlay_toggle(s);
        return true;
    }

    /* Super+Tab — cycle layout */
    if (sym == XKB_KEY_Tab) {
        syn_workspace_t *ws = &s->workspaces[s->active_workspace];
        ws->layout = (ws->layout + 1) % 4;
        static const char *lnames[] = {"tiling","floating","monocle","AI"};
        wlr_log(WLR_INFO, "synui: layout → %s", lnames[ws->layout]);
        layout_apply(s, ws);
        return true;
    }

    /* Super+H / Super+L — shrink / grow the master column */
    if (!shift && lower == XKB_KEY_h) {
        layout_adjust_master(s, &s->workspaces[s->active_workspace], -0.05f);
        return true;
    }
    if (!shift && lower == XKB_KEY_l) {
        layout_adjust_master(s, &s->workspaces[s->active_workspace], +0.05f);
        return true;
    }

    /* Super+Shift+J/K — move focused window down/up the stack */
    if (shift && lower == XKB_KEY_j) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, 1);
        return true;
    }
    if (shift && lower == XKB_KEY_k) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, -1);
        return true;
    }

    /* Super+J/K — focus next/prev */
    if (lower == XKB_KEY_j) { focus_next(s, 1);  return true; }
    if (lower == XKB_KEY_k) { focus_next(s, -1); return true; }

    /* Super+F — toggle floating (centred placement when floated) */
    if (lower == XKB_KEY_f && s->focused_view) {
        syn_view_t *v = s->focused_view;
        v->floating = !v->floating;
        /* Reflow the remaining tiled windows first, then place this one. */
        layout_apply(s, &s->workspaces[s->active_workspace]);
        if (v->floating) {
            layout_float_place(s, v);
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
        }
        return true;
    }

    /* Super+M — maximize */
    if (lower == XKB_KEY_m && s->focused_view) {
        s->focused_view->maximized = !s->focused_view->maximized;
        view_set_maximized(s->focused_view, s->focused_view->maximized);
        return true;
    }

    /* Super+Backspace — quick AI query via synsh */
    if (sym == XKB_KEY_BackSpace) {
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c",
                  "foot -e synsh -c 'syn ask'", NULL);
            _exit(1);
        }
        return true;
    }

    /* Super+1..9 / Super+Shift+1..9 — workspace switch / move window
     * With Shift held, number keysyms become symbols (!@#$...),
     * so we map them back to their number equivalents. */
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int ws = sym - XKB_KEY_1;
        workspace_switch(s, ws);
        return true;
    }

    /* Shifted number keys: !@#$%^&*( → workspace 1..9 (move window) */
    {
        static const xkb_keysym_t shifted_nums[] = {
            XKB_KEY_exclam, XKB_KEY_at, XKB_KEY_numbersign,
            XKB_KEY_dollar, XKB_KEY_percent, XKB_KEY_asciicircum,
            XKB_KEY_ampersand, XKB_KEY_asterisk, XKB_KEY_parenleft
        };
        for (int i = 0; i < 9; i++) {
            if (sym == shifted_nums[i] && shift) {
                if (s->focused_view)
                    workspace_move_view(s, s->focused_view, i);
                return true;
            }
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

static void server_new_keyboard(syn_server_t *s, struct wlr_input_device *dev)
{
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(dev);
    syn_keyboard_t *kb = calloc(1, sizeof(*kb));
    kb->server = s;
    kb->wlr_keyboard = wlr_kb;

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL,
                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_kb->events.key, &kb->key);
    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&dev->events.destroy, &kb->destroy);

    wlr_seat_set_keyboard(s->seat, wlr_kb);
    wl_list_insert(&s->keyboards, &kb->link);
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
    if (min_w < 2 * BORDER_WIDTH + 20) min_w = 2 * BORDER_WIDTH + 20;
    if (min_h < 2 * BORDER_WIDTH + 20) min_h = 2 * BORDER_WIDTH + 20;

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
static void server_new_pointer(syn_server_t *s, struct wlr_input_device *dev)
{
    wlr_cursor_attach_input_device(s->cursor, dev);
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    notify_activity(s);
    wlr_cursor_move(s->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    if (s->cursor_mode == SYNUI_CURSOR_MOVE)   { process_cursor_move(s);   return; }
    if (s->cursor_mode == SYNUI_CURSOR_RESIZE) { process_cursor_resize(s); return; }

    /* Pass to the surface under the cursor (toplevel, layer surface, or popup) */
    double sx, sy;
    struct wlr_surface *surface =
        surface_at(s, s->cursor->x, s->cursor->y, NULL, &sx, &sy);
    if (surface) {
        wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, event->time_msec, sx, sy);
    } else {
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
        wlr_seat_pointer_notify_clear_focus(s->seat);
    }
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    notify_activity(s);
    wlr_cursor_warp_absolute(s->cursor, &event->pointer->base,
                             event->x, event->y);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    if (s->cursor_mode == SYNUI_CURSOR_MOVE)   { process_cursor_move(s);   return; }
    if (s->cursor_mode == SYNUI_CURSOR_RESIZE) { process_cursor_resize(s); return; }

    double sx, sy;
    struct wlr_surface *surface =
        surface_at(s, s->cursor->x, s->cursor->y, NULL, &sx, &sy);
    if (surface) {
        wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, event->time_msec, sx, sy);
    } else {
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
        wlr_seat_pointer_notify_clear_focus(s->seat);
    }
}

static void server_cursor_button(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_button);
    struct wlr_pointer_button_event *event = data;
    notify_activity(s);

    /* A release always ends an in-progress grab and is swallowed. */
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
        s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH) {
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
        s->grabbed_view = NULL;
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y,
                                    &surface, &sx, &sy);

        /* Super + drag begins an interactive move/resize; the button is not
         * forwarded to the client. */
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
        uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
        if (view && (mods & WLR_MODIFIER_LOGO)) {
            if (event->button == BTN_LEFT) {
                begin_interactive(view, SYNUI_CURSOR_MOVE);
                return;
            }
            if (event->button == BTN_RIGHT) {
                begin_interactive(view, SYNUI_CURSOR_RESIZE);
                return;
            }
        }
        if (view) focus_view(s, view, surface);
    }

    wlr_seat_pointer_notify_button(s->seat, event->time_msec,
                                    event->button, event->state);
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

/* ── New input device ────────────────────────────────────── */
static void server_new_input(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_input);
    struct wlr_input_device *dev = data;

    switch (dev->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: server_new_keyboard(s, dev); break;
    case WLR_INPUT_DEVICE_POINTER:  server_new_pointer(s, dev);  break;
    default: break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&s->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s->seat, caps);
}

static void server_request_set_selection(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(s->seat, event->source, event->serial);
}

/* ── Setup all input listeners ───────────────────────────── */
void input_setup(syn_server_t *s)
{
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
}
