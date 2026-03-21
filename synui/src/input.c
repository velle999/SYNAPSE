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
 *   Super+L              Cycle layout (tile → monocle → AI → floating → tile)
 *   Super+J/K            Focus next/prev window
 *   Super+Shift+J/K      Move window in stack
 *   Super+H/L            Adjust master factor
 *   Super+F              Toggle floating
 *   Super+M              Toggle maximize
 *   Super+1..9           Switch to workspace N
 *   Super+Shift+1..9     Move focused window to workspace N
 *   Super+Backspace      Spawn: syn ask (quick AI query)
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "synui.h"

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

    /* Update border colors */
    if (prev && prev != view)
        view_update_borders(prev);
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

syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy)
{
    struct wlr_scene_node *node =
        wlr_scene_node_at(&s->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return NULL;

    struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surf = wlr_scene_surface_try_from_buffer(buf);
    if (!scene_surf) return NULL;

    *surface = scene_surf->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data)
        tree = tree->node.parent;

    return tree ? tree->node.data : NULL;
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

    /* Pick border color */
    float color[4];

    if (view->security == WIN_SECURE_ALERT ||
        view->security == WIN_SECURE_DENIED) {
        float c[] = COLOR_BORDER_WARN;
        memcpy(color, c, sizeof(color));
    } else if (view->ai_ctx.has_ctx) {
        float c[] = COLOR_BORDER_AI;
        memcpy(color, c, sizeof(color));
    } else if (/* focused */ 0) {
        float c[] = COLOR_BORDER_FOCUS;
        memcpy(color, c, sizeof(color));
    } else {
        float c[] = COLOR_BORDER_NORM;
        memcpy(color, c, sizeof(color));
    }

    int x = view->x, y = view->y, w = view->w, h = view->h;
    int bw = BORDER_WIDTH;

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
    MAKE_BORDER(border_left,   x,        y+bw,     bw, h-2*bw);
    MAKE_BORDER(border_right,  x+w-bw,   y+bw,     bw, h-2*bw);

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
        focus_view(s, next, next->xdg_surface->surface);
}

static bool handle_keybinding(syn_server_t *s, xkb_keysym_t sym,
                               uint32_t modifiers)
{
    bool super = (modifiers & WLR_MODIFIER_LOGO) != 0;
    bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;

    if (!super) return false;

    /* Super+Shift+Q — quit */
    if (shift && sym == XKB_KEY_q) {
        wl_display_terminate(s->display);
        return true;
    }

    /* Super+Q — close focused window */
    if (!shift && sym == XKB_KEY_q) {
        if (s->focused_view)
            wlr_xdg_toplevel_send_close(s->focused_view->xdg_surface->toplevel);
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
    if (!shift && sym == XKB_KEY_a) {
        overlay_toggle(s);
        return true;
    }

    /* Super+L — cycle layout */
    if (!shift && sym == XKB_KEY_l) {
        syn_workspace_t *ws = &s->workspaces[s->active_workspace];
        ws->layout = (ws->layout + 1) % 4;
        static const char *lnames[] = {"tiling","floating","monocle","AI"};
        wlr_log(WLR_INFO, "synui: layout → %s", lnames[ws->layout]);
        layout_apply(s, ws);
        return true;
    }

    /* Super+J/K — focus next/prev */
    if (sym == XKB_KEY_j) { focus_next(s, 1);  return true; }
    if (sym == XKB_KEY_k) { focus_next(s, -1); return true; }

    /* Super+F — toggle floating */
    if (sym == XKB_KEY_f && s->focused_view) {
        s->focused_view->floating = !s->focused_view->floating;
        layout_apply(s, &s->workspaces[s->active_workspace]);
        return true;
    }

    /* Super+M — maximize */
    if (sym == XKB_KEY_m && s->focused_view) {
        s->focused_view->maximized = !s->focused_view->maximized;
        wlr_xdg_toplevel_set_maximized(
            s->focused_view->xdg_surface->toplevel,
            s->focused_view->maximized);
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

    /* Super+1..9 — switch workspace */
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int ws = sym - XKB_KEY_1;
        if (shift) {
            if (s->focused_view)
                workspace_move_view(s, s->focused_view, ws);
        } else {
            workspace_switch(s, ws);
        }
        return true;
    }

    return false;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    syn_keyboard_t *kb = wl_container_of(listener, kb, key);
    syn_server_t *s = kb->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *wlr_kb = kb->wlr_keyboard;

    /* Translate keycode to keysym */
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(wlr_kb->xkb_state, keycode, &syms);
    uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_kb);

    bool handled = false;

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

/* ── Pointer ─────────────────────────────────────────────── */
static void server_new_pointer(syn_server_t *s, struct wlr_input_device *dev)
{
    wlr_cursor_attach_input_device(s->cursor, dev);
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(s->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* Pass to focused surface */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);
    if (view) {
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
    wlr_seat_pointer_notify_button(s->seat, event->time_msec,
                                    event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y,
                                    &surface, &sx, &sy);
        if (view) focus_view(s, view, surface);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
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
