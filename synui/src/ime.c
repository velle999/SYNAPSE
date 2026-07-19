/*
 * ime.c — input method support (text-input-v3 + input-method-v2)
 *
 * synui advertised neither protocol, so every toolkit disabled its IME:
 * foot said it outright ("text input interface not implemented by compositor;
 * IME will be disabled"). No IME means no CJK, no compose key, no emoji picker
 * — anything that isn't a direct keysym→character mapping simply cannot be
 * typed. That's a hard blocker for anyone who doesn't type Latin.
 *
 * The relay
 * ---------
 * Two protocols meet here and neither talks to the other:
 *
 *   text-input-v3    the *application* (Firefox, foot): "I have a text field
 *                    here, at this cursor rectangle, of this content type."
 *   input-method-v2  the *IME* (fcitx5, ibus): "the user typed ni-hao; here is
 *                    the preedit 你好, now commit it."
 *
 * The compositor is the switchboard. It must:
 *   - tell the IME which text field is focused (activate/deactivate), following
 *     keyboard focus;
 *   - forward the app's surrounding text / content type / cursor rect to the IME;
 *   - forward the IME's preedit and commit strings back into the app;
 *   - hand raw keys to the IME first when it grabs the keyboard, so the IME sees
 *     the keystrokes it is composing from instead of the app receiving them;
 *   - place the IME's candidate popup ("你好 / 泥號 / …") next to the caret.
 *
 * Only one text input can be active at a time (the one on the focused surface,
 * belonging to the focused client), and only the newest IME is used — a second
 * one is told it's unavailable rather than silently fighting for input.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>

#include "synui.h"

/* ── Helpers ─────────────────────────────────────────────── */
static struct wl_client *surface_client(struct wlr_surface *surface)
{
    return surface ? wl_resource_get_client(surface->resource) : NULL;
}

/* The text input that should be receiving IME output: the one that is enabled
 * and focused on the surface that currently has keyboard focus. */
static syn_text_input_t *relay_focused_text_input(syn_ime_t *relay)
{
    syn_text_input_t *ti;
    wl_list_for_each(ti, &relay->text_inputs, link) {
        if (ti->input->focused_surface && ti->input->current_enabled)
            return ti;
    }
    return NULL;
}

/* ── IME popup (candidate window) ────────────────────────── */
/*
 * Park the popup just below the caret. The text input reports its cursor
 * rectangle in *surface-local* coordinates, so add the focused window's
 * position to get layout space. If it would fall off the bottom of the output,
 * flip it above the caret — a candidate list you can't see is useless.
 */
static void ime_popup_reposition(syn_ime_popup_t *popup)
{
    syn_ime_t *relay = popup->relay;
    syn_server_t *s  = relay->server;

    if (!popup->tree || !popup->popup->surface->mapped) return;

    syn_text_input_t *ti = relay_focused_text_input(relay);
    if (!ti) return;

    syn_view_t *view = s->focused_view;
    if (!view || !view->mapped) return;

    /* The caret rect, in layout space. The client area — not the frame — is
     * what surface-local coordinates are relative to. */
    struct wlr_box content;
    view_content_box(view, &content);

    struct wlr_box caret = ti->input->current.cursor_rectangle;
    int px = content.x + caret.x;
    int py = content.y + caret.y + caret.height;

    int pw = popup->popup->surface->current.width;
    int ph = popup->popup->surface->current.height;

    /* Keep it on the monitor the window is on. */
    struct wlr_box out;
    output_usable_box_of(s, view->output ? view->output
                                         : server_focused_output(s), &out);
    if (px + pw > out.x + out.width)  px = out.x + out.width - pw;
    if (px < out.x) px = out.x;
    if (py + ph > out.y + out.height)          /* no room below → flip above */
        py = content.y + caret.y - ph;
    if (py < out.y) py = out.y;

    wlr_scene_node_set_position(&popup->tree->node, px, py);

    /* Tell the IME where the text field is, so it can size/anchor itself. */
    wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &caret);
}

static void ime_popup_surface_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_ime_popup_t *popup = wl_container_of(listener, popup, surface_commit);
    ime_popup_reposition(popup);
}

static void ime_popup_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_ime_popup_t *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->surface_commit.link);
    wl_list_remove(&popup->link);
    if (popup->tree)
        wlr_scene_node_destroy(&popup->tree->node);
    free(popup);
}

static void ime_new_popup_surface(struct wl_listener *listener, void *data)
{
    syn_ime_t *relay = wl_container_of(listener, relay, im_new_popup);
    struct wlr_input_popup_surface_v2 *ps = data;

    syn_ime_popup_t *popup = calloc(1, sizeof(*popup));
    if (!popup) return;
    popup->popup = ps;
    popup->relay = relay;

    /* The candidate list must float over the window it belongs to — the OVERLAY
     * layer is where the compositor's own always-on-top surfaces live. */
    popup->tree = wlr_scene_subsurface_tree_create(
        relay->server->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
        ps->surface);

    popup->destroy.notify = ime_popup_destroy;
    wl_signal_add(&ps->events.destroy, &popup->destroy);
    popup->surface_commit.notify = ime_popup_surface_commit;
    wl_signal_add(&ps->surface->events.commit, &popup->surface_commit);

    wl_list_insert(&relay->popups, &popup->link);
    ime_popup_reposition(popup);
}

/* ── IME → application ───────────────────────────────────── */
/* The IME committed its state: push preedit / commit / delete straight through
 * to the focused text field. Order matters — delete, then commit, then preedit,
 * then one done() — because that's the order the app applies them in. */
static void ime_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_ime_t *relay = wl_container_of(listener, relay, im_commit);
    struct wlr_input_method_v2 *im = relay->input_method;

    syn_text_input_t *ti = relay_focused_text_input(relay);
    if (!ti || !im) return;

    if (im->current.delete.before_length || im->current.delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(ti->input,
            im->current.delete.before_length,
            im->current.delete.after_length);

    if (im->current.commit_text)
        wlr_text_input_v3_send_commit_string(ti->input, im->current.commit_text);

    if (im->current.preedit.text)
        wlr_text_input_v3_send_preedit_string(ti->input,
            im->current.preedit.text,
            im->current.preedit.cursor_begin,
            im->current.preedit.cursor_end);

    wlr_text_input_v3_send_done(ti->input);
}

/* ── Keyboard grab ───────────────────────────────────────── */
/*
 * While the IME holds the grab, raw keys belong to it, not to the application:
 * typing "nihao" must reach fcitx5 to be composed, not land in the text field
 * as five Latin letters. input.c calls ime_handle_key()/ime_handle_modifiers()
 * before it forwards anything to the seat.
 */
static void ime_grab_keyboard_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_ime_t *relay = wl_container_of(listener, relay, grab_keyboard_destroy);
    wl_list_remove(&relay->grab_keyboard_destroy.link);
    wl_list_init(&relay->grab_keyboard_destroy.link);
    relay->keyboard_grab = NULL;
}

static void ime_grab_keyboard(struct wl_listener *listener, void *data)
{
    syn_ime_t *relay = wl_container_of(listener, relay, im_grab_keyboard);
    struct wlr_input_method_keyboard_grab_v2 *grab = data;

    /* Hand the grab the active keyboard so it inherits its keymap and repeat
     * settings — without this the IME composes against the wrong layout. */
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(relay->server->seat);
    if (kb)
        wlr_input_method_keyboard_grab_v2_set_keyboard(grab, kb);

    relay->keyboard_grab = grab;
    relay->grab_keyboard_destroy.notify = ime_grab_keyboard_destroy;
    wl_signal_add(&grab->events.destroy, &relay->grab_keyboard_destroy);
}

bool ime_handle_key(syn_server_t *s, struct wlr_keyboard *kb,
                    uint32_t time_msec, uint32_t keycode,
                    enum wl_keyboard_key_state state)
{
    syn_ime_t *relay = s->ime;
    if (!relay || !relay->keyboard_grab) return false;
    /* Only steal keys when a text field is actually active — otherwise a
     * lingering grab would swallow every key in the session. */
    if (!relay_focused_text_input(relay)) return false;

    wlr_input_method_keyboard_grab_v2_set_keyboard(relay->keyboard_grab, kb);
    wlr_input_method_keyboard_grab_v2_send_key(relay->keyboard_grab,
                                               time_msec, keycode, state);
    return true;
}

bool ime_handle_modifiers(syn_server_t *s, struct wlr_keyboard *kb)
{
    syn_ime_t *relay = s->ime;
    if (!relay || !relay->keyboard_grab) return false;
    if (!relay_focused_text_input(relay)) return false;

    wlr_input_method_keyboard_grab_v2_set_keyboard(relay->keyboard_grab, kb);
    wlr_input_method_keyboard_grab_v2_send_modifiers(relay->keyboard_grab,
                                                     &kb->modifiers);
    return true;
}

/* ── Application → IME ───────────────────────────────────── */
/* Mirror the text field's state into the IME and (de)activate it. Called when
 * the field is enabled/disabled and on every commit (the app updating its
 * surrounding text or caret). */
static void ime_send_state(syn_ime_t *relay, syn_text_input_t *ti)
{
    struct wlr_input_method_v2 *im = relay->input_method;
    if (!im) return;

    wlr_input_method_v2_send_activate(im);

    if (ti->input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(im,
            ti->input->current.surrounding.text,
            ti->input->current.surrounding.cursor,
            ti->input->current.surrounding.anchor);

    wlr_input_method_v2_send_text_change_cause(im,
        ti->input->current.text_change_cause);

    if (ti->input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(im,
            ti->input->current.content_type.hint,
            ti->input->current.content_type.purpose);

    wlr_input_method_v2_send_done(im);

    /* The caret may have moved; the candidate list follows it. */
    syn_ime_popup_t *popup;
    wl_list_for_each(popup, &relay->popups, link)
        ime_popup_reposition(popup);
}

static void ime_deactivate(syn_ime_t *relay)
{
    if (!relay->input_method) return;
    wlr_input_method_v2_send_deactivate(relay->input_method);
    wlr_input_method_v2_send_done(relay->input_method);
}

static void text_input_enable(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_text_input_t *ti = wl_container_of(listener, ti, enable);
    if (!ti->relay->input_method) return;
    ime_send_state(ti->relay, ti);
}

static void text_input_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_text_input_t *ti = wl_container_of(listener, ti, commit);
    if (!ti->input->current_enabled || !ti->relay->input_method) return;
    ime_send_state(ti->relay, ti);
}

static void text_input_disable(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_text_input_t *ti = wl_container_of(listener, ti, disable);
    ime_deactivate(ti->relay);
}

static void text_input_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_text_input_t *ti = wl_container_of(listener, ti, destroy);

    if (ti->input->current_enabled)
        ime_deactivate(ti->relay);

    wl_list_remove(&ti->enable.link);
    wl_list_remove(&ti->commit.link);
    wl_list_remove(&ti->disable.link);
    wl_list_remove(&ti->destroy.link);
    wl_list_remove(&ti->link);
    free(ti);
}

static void ime_new_text_input(struct wl_listener *listener, void *data)
{
    syn_ime_t *relay = wl_container_of(listener, relay, new_text_input);
    struct wlr_text_input_v3 *wlr_ti = data;

    if (wlr_ti->seat != relay->server->seat) return;

    syn_text_input_t *ti = calloc(1, sizeof(*ti));
    if (!ti) return;
    ti->input = wlr_ti;
    ti->relay = relay;

    ti->enable.notify  = text_input_enable;
    wl_signal_add(&wlr_ti->events.enable,  &ti->enable);
    ti->commit.notify  = text_input_commit;
    wl_signal_add(&wlr_ti->events.commit,  &ti->commit);
    ti->disable.notify = text_input_disable;
    wl_signal_add(&wlr_ti->events.disable, &ti->disable);
    ti->destroy.notify = text_input_destroy;
    wl_signal_add(&wlr_ti->events.destroy, &ti->destroy);

    wl_list_insert(&relay->text_inputs, &ti->link);

    /* The client may have created the text input *after* it already got focus
     * (GTK does), so hand it the current focus straight away rather than
     * waiting for the next focus change that may never come. */
    struct wlr_surface *focused = relay->server->seat->keyboard_state.focused_surface;
    if (focused && surface_client(focused) == wl_resource_get_client(wlr_ti->resource))
        wlr_text_input_v3_send_enter(wlr_ti, focused);
}

/* ── Focus follows the keyboard ──────────────────────────── */
/*
 * Keyboard focus moved. Every text input on the old surface gets a leave (and
 * the IME is deactivated if it was riding one), and any text input belonging to
 * the newly focused client gets an enter. Called from focus_view().
 */
void ime_set_focus(syn_server_t *s, struct wlr_surface *surface)
{
    syn_ime_t *relay = s->ime;
    if (!relay) return;

    syn_text_input_t *ti;
    wl_list_for_each(ti, &relay->text_inputs, link) {
        if (ti->input->focused_surface == surface) continue;   /* no change */

        if (ti->input->focused_surface) {
            if (ti->input->current_enabled)
                ime_deactivate(relay);
            wlr_text_input_v3_send_leave(ti->input);
        }

        if (surface &&
            surface_client(surface) ==
                wl_resource_get_client(ti->input->resource))
            wlr_text_input_v3_send_enter(ti->input, surface);
    }
}

/* ── Input method lifecycle ──────────────────────────────── */
static void ime_input_method_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_ime_t *relay = wl_container_of(listener, relay, im_destroy);

    wl_list_remove(&relay->im_commit.link);
    wl_list_remove(&relay->im_new_popup.link);
    wl_list_remove(&relay->im_grab_keyboard.link);
    wl_list_remove(&relay->im_destroy.link);
    wl_list_init(&relay->im_commit.link);
    wl_list_init(&relay->im_new_popup.link);
    wl_list_init(&relay->im_grab_keyboard.link);
    wl_list_init(&relay->im_destroy.link);

    relay->input_method  = NULL;
    relay->keyboard_grab = NULL;

    /* The IME died while a field was focused (fcitx5 restarting). Re-arm on the
     * next one that appears; the field itself is still perfectly usable, it
     * just falls back to direct keysym input. */
}

static void ime_new_input_method(struct wl_listener *listener, void *data)
{
    syn_ime_t *relay = wl_container_of(listener, relay, new_input_method);
    struct wlr_input_method_v2 *im = data;

    if (im->seat != relay->server->seat) return;

    /* One IME per seat. A second one would fight the first over every keystroke
     * — tell it so rather than letting input go strange. */
    if (relay->input_method) {
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    relay->input_method = im;

    relay->im_commit.notify = ime_commit;
    wl_signal_add(&im->events.commit, &relay->im_commit);
    relay->im_new_popup.notify = ime_new_popup_surface;
    wl_signal_add(&im->events.new_popup_surface, &relay->im_new_popup);
    relay->im_grab_keyboard.notify = ime_grab_keyboard;
    wl_signal_add(&im->events.grab_keyboard, &relay->im_grab_keyboard);
    relay->im_destroy.notify = ime_input_method_destroy;
    wl_signal_add(&im->events.destroy, &relay->im_destroy);

    /* An IME that starts *after* the user already clicked into a text field
     * (the normal case — fcitx5 autostarts behind the session) must be told
     * about it immediately, or the first field of the session never works. */
    syn_text_input_t *ti = relay_focused_text_input(relay);
    if (ti)
        ime_send_state(relay, ti);
}

/* ── Setup / teardown ────────────────────────────────────── */
void ime_setup(syn_server_t *s)
{
    syn_ime_t *relay = calloc(1, sizeof(*relay));
    if (!relay) return;
    relay->server = s;
    wl_list_init(&relay->text_inputs);
    wl_list_init(&relay->popups);
    wl_list_init(&relay->grab_keyboard_destroy.link);
    wl_list_init(&relay->im_commit.link);
    wl_list_init(&relay->im_new_popup.link);
    wl_list_init(&relay->im_grab_keyboard.link);
    wl_list_init(&relay->im_destroy.link);

    s->text_input_mgr   = wlr_text_input_manager_v3_create(s->display);
    s->input_method_mgr = wlr_input_method_manager_v2_create(s->display);

    relay->new_text_input.notify = ime_new_text_input;
    wl_signal_add(&s->text_input_mgr->events.text_input,
                  &relay->new_text_input);
    relay->new_input_method.notify = ime_new_input_method;
    wl_signal_add(&s->input_method_mgr->events.input_method,
                  &relay->new_input_method);

    s->ime = relay;
}

void ime_destroy(syn_server_t *s)
{
    syn_ime_t *relay = s->ime;
    if (!relay) return;

    /* wlroots asserts empty listener lists when it tears the managers down. */
    wl_list_remove(&relay->new_text_input.link);
    wl_list_remove(&relay->new_input_method.link);
    if (relay->input_method) {
        wl_list_remove(&relay->im_commit.link);
        wl_list_remove(&relay->im_new_popup.link);
        wl_list_remove(&relay->im_grab_keyboard.link);
        wl_list_remove(&relay->im_destroy.link);
    }
    if (relay->keyboard_grab)
        wl_list_remove(&relay->grab_keyboard_destroy.link);

    free(relay);
    s->ime = NULL;
}
