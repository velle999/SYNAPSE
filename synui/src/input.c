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
#include <wlr/types/wlr_virtual_pointer_v1.h>

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

/*
 * Was this event only ever going to wake the screensaver?
 *
 * ANY input takes the saver down, and the input that took it down must not also
 * reach whatever is underneath. That is the classic screensaver bug: you nudge
 * a machine awake with the space bar and the space bar lands in the editor that
 * had focus, or the click that dismissed it activates a button you could not
 * see. So the wake is a keystroke you SPEND.
 *
 * Asked BEFORE notify_activity(), because power_notify_activity() is what
 * dismisses the saver — by the time that has run, saver_active() is false and
 * there is nothing left to detect.
 *
 * Motion is deliberately not routed through this: moving the mouse should wake
 * the screen and still move the cursor, exactly as it does over the lock.
 */
static inline bool saver_ate_event(syn_server_t *s)
{
    if (!saver_active(s)) return false;
    saver_dismiss(s, true);
    notify_activity(s);      /* still an activity event: rearm the stages */
    return true;
}

/* ── Focus ───────────────────────────────────────────────── */
/*
 * Focus follows the pointer, when focus_mode says so.
 *
 * THE CLICK PATH IS NOT TOUCHED. Clicking a window focuses it under every
 * mode, exactly as before, so a pointer mode can only ever ADD a way to focus
 * something. That is deliberate: a bug in the logic below leaves the desktop
 * usable rather than leaving a window that cannot be reached.
 *
 * Everything that owns the pointer is refused first, and each for its own
 * reason rather than as a blanket "are we busy":
 *
 *   - a held button is an implicit grab (see pointer_update_focus): moving
 *     keyboard focus mid-drag is how a text selection ends up typing into the
 *     window you dragged across.
 *   - an interactive move/resize is a grab we started ourselves.
 *   - a lock screen must never hand focus to anything behind it.
 *   - a layer-shell surface with keyboard interactivity (the launcher, a
 *     panel) has asked for the keyboard; the pointer does not outrank that.
 */
static int focus_follow_fire(void *data);

static void focus_follow_pointer(syn_server_t *s, uint32_t time_msec)
{
    (void)time_msec;
    if (!s || s->config.focus_mode == SYN_FOCUS_CLICK) return;
    if (s->locked) return;
    if (s->seat->pointer_state.button_count > 0) return;
    if (s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH || s->grabbed_view) return;

    if (s->config.focus_delay_ms <= 0) {
        focus_follow_fire(s);
        return;
    }

    if (!s->focus_follow_timer) {
        struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
        s->focus_follow_timer = wl_event_loop_add_timer(loop, focus_follow_fire, s);
        if (!s->focus_follow_timer) {          /* no timer: behave as 0 delay */
            focus_follow_fire(s);
            return;
        }
    }
    /* Re-arming pushes it out, so it only expires once the pointer stops. */
    wl_event_source_timer_update(s->focus_follow_timer, s->config.focus_delay_ms);
}

/* Re-queries what is under the cursor rather than trusting anything recorded
 * when the timer was armed — see the note on focus_follow_timer. */
static int focus_follow_fire(void *data)
{
    syn_server_t *s = data;
    if (!s || s->config.focus_mode == SYN_FOCUS_CLICK) return 0;
    if (s->locked || s->seat->pointer_state.button_count > 0) return 0;
    if (s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH || s->grabbed_view) return 0;

    double sx, sy;
    struct wlr_surface *surface = NULL;
    syn_view_t *view = view_at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    if (!view) {
        /* Over the desktop, our own chrome, or a layer surface. THIS is the
         * only difference between the two pointer modes: sloppy leaves the
         * last window focused so the keyboard still goes somewhere useful
         * while the pointer is parked on the wallpaper; strict takes focus
         * away, which is what "strictly under mouse" means. */
        if (s->config.focus_mode == SYN_FOCUS_STRICT && s->focused_view)
            focus_view(s, NULL, NULL);
        return 0;
    }
    if (view == s->focused_view) return 0;

    focus_view(s, view, view_surface(view));
    return 0;
}

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

    /* Monocle keys visibility off the focused view — layout_monocle enables
     * exactly one scene node per output and disables the rest — but nothing on
     * the focus path reflowed, and that toggle has only ever run from
     * layout_apply(). So Alt+Tab, Super+J/K, the dock and a click all moved the
     * keyboard to a window that stayed hidden, leaving you typing into a window
     * you could not see while the old one sat on screen; it only came right
     * when something unrelated (a window opening or closing, a desktop switch,
     * Super+F, retile) happened to reflow the desktop. layout.c's own header
     * claimed "cycle with Alt+Tab" — this is what makes that true.
     *
     * Gated on the two layouts whose windows can be hidden. The others only
     * place geometry, so reflowing them on every focus change would be a
     * configure storm for no visible gain. Safe against recursion: layout_apply
     * never calls back into focus_view.
     *
     * niri is here for the same reason wearing a different hat: its strip is
     * scrolled to wherever the focus is, and a column that is not fully on
     * screen is not drawn at all (layout_niri). Without this, Alt+Tab or a
     * click on the dock would move the keyboard to a window still parked off
     * the side of the monitor. */
    if (view->workspace && (view->workspace->layout == LAYOUT_MONOCLE ||
                            view->workspace->layout == LAYOUT_NIRI))
        layout_apply(s, view->workspace);

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

/* The view a scene node ultimately belongs to, or NULL for one that is not a
 * window's (a layer surface, the wallpaper, our own chrome). */
static syn_view_t *node_owner_view(struct wlr_scene_node *node)
{
    struct wlr_scene_tree *tree = node ? node->parent : NULL;
    while (tree && !tree->node.data)
        tree = tree->node.parent;
    return tree ? tree->node.data : NULL;
}

/*
 * ⚠ EVERY PIXEL OF A FULLSCREEN CLIENT'S BOX IS THE CLIENT'S, NOT NOBODY'S.
 *
 * view_fullscreen_rescale() fits a sub-native surface inside the fullscreen
 * frame and centres it, so the frame carries bars no surface is painted in.
 * The scene knows nothing is drawn there and answers NULL, and NULL is what
 * ends mouse capture: pointer_update_focus() clears pointer focus, the client
 * stops receiving motion at all, and for an Xwayland game it is terminal —
 * Xwayland only asks for a pointer lock while its surface holds pointer focus,
 * and it deactivates the one it has when focus goes. Deactivating a ONESHOT
 * constraint destroys it outright.
 *
 * Nothing else can be under those bars. They are inside one window's own
 * fullscreen frame, that window covers its whole output, and anything drawn
 * above it (a panel, a layer surface, a popup) is found by the scene walk and
 * wins before this is reached. So the bars answer for the window they belong
 * to, at the nearest point of its picture — which is the same coordinate the
 * confine would have clamped the cursor to.
 *
 * Reported three times as three bugs: "the mouse drifts to another monitor",
 * "I can't look right", "it moves an inch then sticks". 512 stopped the cursor
 * REACHING a bar; this makes reaching one harmless, which is what a game that
 * genuinely letterboxes (4:3 on a 16:9 screen) needs.
 */
static struct wlr_surface *fullscreen_bar_surface_at(
    syn_server_t *s, double lx, double ly, struct wlr_scene_node *hit,
    syn_view_t **view_out, double *sx, double *sy)
{
    /* Something IS drawn here and it belongs to another window — not a bar. */
    syn_view_t *owner = node_owner_view(hit);

    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->mapped || !v->fullscreen || !v->is_xwayland) continue;
            if (owner && owner != v) continue;
            if (lx < v->x || lx >= v->x + v->w ||
                ly < v->y || ly >= v->y + v->h) continue;

            struct wlr_surface *surf = view_surface(v);
            if (!surf) continue;
            int surf_w = surf->current.width, surf_h = surf->current.height;
            if (surf_w <= 0 || surf_h <= 0) continue;

            /* view_scaled_content_box() answers 0 for a view whose tree is not
             * on screen, which is also how a window on another workspace is
             * ruled out. */
            struct wlr_box c;
            if (!view_scaled_content_box(v, &c)) continue;
            if (c.width <= 0 || c.height <= 0) continue;
            /* ⚠ NOT ONLY WHEN THERE ARE BARS.
             *
             * This skipped a picture that fills its frame, reasoning that with
             * no bars the scene walk had already answered for every point of
             * it. It has not.
             *
             * MEASURED 2026-08-26, Cyberpunk 2077 filling DP-3 exactly —
             * content 1080,1080 2560x1440, the whole output, so this early-out
             * declined it. Pointer focus dropped to NULL at x 3639 and at
             * y 2519 and at no other coordinate: fourteen flips in twelve
             * seconds, every one of them on one of those two lines. They are
             * the output's last column and last row, which is precisely where
             * game_confine_cursor() parks the cursor every time the user
             * pushes against the edge — during mouse-look, constantly. Each
             * flip clears pointer focus, and Xwayland only holds a pointer
             * lock while its surface has it.
             *
             * So the rule is the wider one the comment above already argues:
             * a fullscreen window owns every pixel of its own box for input,
             * whether the pixel is a letterbox bar or an edge the scene walk
             * declined. The clamp below answers with the nearest point of the
             * picture, which for a picture that fills the frame is the pixel
             * asked about.
             */

            /* The nearest point of the picture, in ITS coordinates — the
             * point itself when it is already inside one. game.c owns the
             * reckoning so a test can hold it still. */
            struct wlr_box vb = { v->x, v->y, v->w, v->h };
            double cx, cy;
            if (!game_fullscreen_owns_point(&vb, &c, lx, ly, &cx, &cy)) continue;
            if (sx) *sx = (cx - c.x) * (double)surf_w / c.width;
            if (sy) *sy = (cy - c.y) * (double)surf_h / c.height;
            if (view_out) *view_out = v;
            return surf;
        }
    }
    return NULL;
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
    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return fullscreen_bar_surface_at(s, lx, ly, node, view_out, sx, sy);

    /* Never the drag icon.
     *
     * It is the topmost thing in the scene and it is parked exactly ON the
     * cursor, so during any drag that carries one — Qt's, GTK's, Dolphin's —
     * this hit test answers "there is a surface here" for every point on the
     * screen. That is not a near miss: point_is_desktop() is `!view &&
     * !surface`, so the desktop stopped being a drop target the moment a
     * client attached an icon to its drag, and deskdrop.c refused every drop
     * in silence.
     *
     * wl_data_device is explicit that the icon's input region is cleared and
     * that set_input_region is ignored for it afterwards; wlroots does not
     * enforce it (types/data_device/wlr_drag.c has no input-region handling at
     * all) and neither did we, so the rule is applied here, where every caller
     * gets it. A drag icon is never an input target for anyone. */
    if (s->drag_icon_tree) {
        for (struct wlr_scene_node *n = node; n; n = &n->parent->node) {
            if (n == &s->drag_icon_tree->node) return NULL;
            if (!n->parent) break;
        }
    }

    struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surf = wlr_scene_surface_try_from_buffer(buf);
    if (!scene_surf)
        return fullscreen_bar_surface_at(s, lx, ly, node, view_out, sx, sy);

    if (view_out) *view_out = node_owner_view(node);
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

    /* Which layout the seat is on, taken from the keyboard that just changed.
     *
     * ⚠ FIRST, above every early return below. xkb's own `grp:` options move
     * the group without going anywhere near kbdlayout.c, and the IME grab and
     * the Alt+Tab commit both return early — so anywhere further down, a switch
     * made with the configured chord would be invisible to the lock screen's
     * chip, which would then be a label that lies about the keys. */
    kbd_layout_observe(kb->server, kb->wlr_keyboard);

    /* Alt let go ends an Alt+Tab cycle and commits the window we landed on.
     *
     * This is the only place that can see it: a modifier release produces a
     * modifiers event, not a key event, so keyboard_handle_key never hears
     * about it. Checked before the IME early-return — a cycle left "active"
     * because fcitx5 happened to be grabbing the keyboard would freeze the MRU
     * order for every later focus change, which is a far stranger bug than any
     * it could save. */
    if (!(wlr_keyboard_get_modifiers(kb->wlr_keyboard) & WLR_MODIFIER_ALT)) {
        if (kb->server->alttab.active)
            alttab_finish(kb->server);
        /* The same release, for the other switcher: with `alt_tab_style` on
         * mission control the cycle is a walk across the overview's tiles, and
         * letting go has to pick the one it landed on. A no-op unless the
         * overview was opened by that gesture, so a mission control opened from
         * the control panel is not dismissed by a passing Alt. */
        overview_alt_commit(kb->server);
    }

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

    /* ⚠ THE SCOPE IS "ON SCREEN", NOT "WORKSPACE N". It used to be a range of
     * workspace indices — [active, active+1) — which is the same thing only
     * while every monitor shows the same desktop. Under per-monitor desktops it
     * is not: two screens can be on 2 and 5, and a range would hide half of
     * what the user is looking at from Alt-Tab. view_workspace_shown() asks the
     * question that is right in both modes, per window, against the monitor
     * that window actually lives on. */
    for (int w = 0; w < WORKSPACE_MAX && n < max; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->mapped) continue;
            if (!s->config.alt_tab_all_desktops && !view_workspace_shown(v))
                continue;
            if (!v->minimized || s->config.alt_tab_minimized) {
                if (n >= max) break;
                out[n++] = v;
            }
        }
    }

    /* ⚠ AND THE WINDOWS THAT ARE NOT ON A WORKSPACE LIST AT ALL.
     *
     * A fullscreen X11 game unmaps itself whenever it loses focus (xw_unmap
     * explains why), and an unmapped view is off its workspace list — so the
     * loop above cannot see the very window the user is trying to get back to.
     * Alt-Tab out of Cyberpunk 2077 and it is in no switcher at all; Steam is
     * the only route back.
     *
     * s->xw_views is the one list that still holds it. Offered on the same
     * terms as any other minimised window (alt_tab_minimized, on by default),
     * and alttab_reveal() asks the client to restore rather than trying to
     * focus a surface that is not mapped. */
    if (s->config.alt_tab_minimized && n < max) {
        syn_view_t *v;
        wl_list_for_each(v, &s->xw_views, xw_link) {
            if (n >= max) break;
            if (v->mapped || v->override_redirect) continue;
            if (!v->minimized || !v->xsurface) continue;
            if (!v->workspace) continue;
            if (!s->config.alt_tab_all_desktops && !view_workspace_shown(v))
                continue;
            out[n++] = v;
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
    (void)s;
    return !v->minimized && view_workspace_shown(v);
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
    /* On the monitor the window LIVES on, not the focused one: under
     * per-monitor desktops switching the screen in front of us to that desktop
     * would show its share of it — which is not where this window is. */
    if (v->workspace && !view_workspace_shown(v))
        workspace_switch_on(s, v->output, v->workspace->index);

    /* An X11 window that minimised itself is UNMAPPED: there is no buffer to
     * raise and no surface to hand the keyboard to. All that can be done is
     * tell the client it is no longer iconified and that it is active, which
     * is what makes Wine map it again; xw_map() takes over from there.
     *
     * ⚠ The minimised flag is deliberately NOT cleared here — xw_map() clears
     * it when the window really comes back. Clearing it now would drop the
     * window out of alttab_candidates() the moment a client ignored the
     * restore, and unreachable-for-good is the bug this is fixing. */
    if (!v->mapped) {
        if (v->is_xwayland && v->xsurface) {
            view_set_minimized(v, 0);
            wlr_xwayland_surface_activate(v->xsurface, true);
            wlr_xwayland_surface_restack(v->xsurface, NULL, XCB_STACK_MODE_ABOVE);
        }
        return;
    }

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

/* The fork/exec itself lives in spawntoggle.c, next to the one caller that
 * needs the pid back. */
static void spawn(const char *cmd)
{
    synui_spawn_pid(cmd);
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
 *   the bar  quickshell is handed the keyboard properly
 *            (PanelWindow { focusable: true } — ON-DEMAND, and
 *            layer_surface_map() grants it to anything not NONE), so the
 *            original objection is simply gone and the menu lives with the bar
 *            it belongs to.
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
/*
 * Ask the bar to do something, naming the focused output.
 *
 * Split out of synui_start_menu_open() when the volume mixer became the second
 * caller. The bar owns two things synui can only ask for — the start menu and
 * the mixer — and both need the same two facts: which target, and which
 * monitor has focus.
 *
 * execvp, not spawn(): spawn() goes through `/bin/sh -c` and this interpolates
 * an output name. Those come from the kernel rather than from a user, but a
 * shell in the path is a shell to get wrong later, and nothing here needs one.
 */
void synui_bar_ipc_arg(syn_server_t *s, const char *target, const char *fn,
                       const char *arg)
{
    (void)s;
    if (fork() == 0) {
        setsid();
        synui_child_reset_signals();
        execlp("synui-bar", "synui-bar", "ipc", "call", target, fn,
               arg ? arg : "", (char *)NULL);
        _exit(1);
    }
}

void synui_bar_ipc(syn_server_t *s, const char *target, const char *fn)
{
    syn_output_t *o = server_focused_output(s);
    synui_bar_ipc_arg(s, target, fn,
                      (o && o->wlr_output && o->wlr_output->name)
                          ? o->wlr_output->name : "");
}

/*
 * …and the same call to the WELCOME GUIDE, which is not the bar.
 *
 * ⚠ A SEPARATE HELPER BECAUSE IT IS A SEPARATE PROCESS. synui_bar_ipc() talks
 * to whichever shell `bar_shell` started, and the guide is deliberately not in
 * either of them — two bars ship, and a guide inside one would not exist for
 * anyone running the other. synui-welcome(1) starts the guide when nothing is
 * listening, which is the half `quickshell ipc` alone cannot do.
 *
 * execvp rather than spawn(): spawn() goes through `/bin/sh -c` and this
 * interpolates an output name. Those come from the kernel rather than from a
 * user, but a shell in the path is a shell to get wrong later, and nothing here
 * needs one.
 */
static void exec_named_output(syn_server_t *s, const char *prog, const char *fn)
{
    syn_output_t *o = server_focused_output(s);
    const char *out = (o && o->wlr_output && o->wlr_output->name)
                          ? o->wlr_output->name : "";
    if (fork() == 0) {
        setsid();
        synui_child_reset_signals();
        execlp(prog, prog, fn, out, (char *)NULL);
        _exit(1);
    }
}

void synui_welcome_ipc(syn_server_t *s, const char *fn)
{
    exec_named_output(s, "synui-welcome", fn);
}

/*
 * …and the wallhaven browser, which is a third process for the same reason.
 *
 * ⛔ THE OUTPUT IS THE POINT OF THIS CALL. A layer-shell client is told nothing
 * about focus by any Wayland protocol, so a browser started with no output puts
 * a window on EVERY screen — which is what shipped in 590 and 591, reported as
 * the browser opening on all monitors at once. synui is the one process that
 * knows which monitor the key was pressed on, and it is answering that keypress
 * as it makes this call.
 */
void synui_wallhaven_ipc(syn_server_t *s, const char *fn)
{
    exec_named_output(s, "synui-wallhaven", fn);
}

/*
 * What the START KEY opens: the Super tap and the `start_menu` action, which is
 * all that is bound to it. Desktop ▸ Start menu (start_menu_style) picks one of
 * the three.
 *
 * ⚠ NOT the dock's grid-of-dots, and not the bar's start button. A key is a
 * keystroke with no picture on it, so it is free to mean whatever this row says.
 * A BUTTON is its own label: the grid-of-dots means the application overlay and
 * the bar's start button means the bar's menu, and neither changes because of a
 * setting somewhere else. Sending the dock button through here is what made
 * pressing the app-grid icon open Rofi.
 *
 * Read at the point of use rather than resolved at config load, so changing the
 * row takes effect on the next keypress with nothing to reload.
 */
void synui_start_menu_open(syn_server_t *s)
{
    switch (s->config.start_menu_style) {
    case SYN_START_MENU_APPGRID:
        appgrid_toggle(s);
        break;
    case SYN_START_MENU_ROFI:
        /* `-show drun` and not plain rofi: this row means "my applications", and
         * bare rofi opens whatever mode its own config defaults to — which on a
         * fresh install is the window switcher. A user who wants other flags has
         * `tap_action = spawn rofi …`, which is finer-grained than this row can
         * be and is untouched by it. */
        /* One string, shared with the Super+= bind — see SYN_ROFI_DRUN. */
        synui_spawn(SYN_ROFI_DRUN);
        break;
    case SYN_START_MENU_BAR:
    default:
        synui_bar_ipc(s, "menu", "toggle");
        break;
    }
}

/* How far one press slides a floating window, and how much of it must stay
 * inside the usable box. The keep-visible margin is the point: a window driven
 * off the edge by a held-down arrow key is a window that can only be recovered
 * from a terminal, and unlike a mouse drag there is no pointer stopping at the
 * screen edge to make that obvious as it happens. */
#define WINDOW_MOVE_STEP     40
#define WINDOW_MOVE_KEEP_VIS 64

/*
 * Super+arrow (Super+Shift+arrow before 438). See the comment on these binds
 * in config.c for why one chord means two different things.
 *
 * dx/dy are -1, 0 or +1.
 */
static void window_move_key(syn_server_t *s, syn_view_t *v, int dx, int dy)
{
    /* A fullscreen window has no position to move and no place in the flow to
     * move through, so there is nothing this could do to it that would not be a
     * surprise. Leave it alone rather than silently dropping it out of
     * fullscreen. */
    if (v->fullscreen) return;

    /*
     * ⛔ `v->floating` IS THE WINDOW'S FLAG, NOT THE DESKTOP'S — the same trap
     * move_output fell into in 436, reached here by a different road.
     *
     * On a FLOATING desktop no window is marked floating; the DESKTOP is. So
     * every window there took the reorder branch: layout_move_in_stack duly
     * rewrote ws->windows and layout_apply ran LAYOUT_FLOATING's pass,
     * layout_float_arrange — which deliberately steps over every window the
     * user has ever placed by hand. The record moved and the pixels did not,
     * and the key read as dead the moment it shipped.
     *
     * The question this branch has to ask is not "is this window floating" but
     * "does a LAYOUT own where this window sits". Only then is its order the
     * only thing there is to move. A floating desktop hands that ownership to
     * the user, which is precisely what the slide below is for — so it belongs
     * on the same side as a floating window.
     *
     * ⚠ This is a WIDER test than move_output's, which also asked
     * `!v->hand_placed`: there the question was whether anything would place
     * the window after its output changed, and float_arrange still places a
     * window nobody has touched. Here a grid slot is not a position the user
     * chose, and pressing a move key IS choosing one — hand_placed below then
     * opts it out of the grid exactly as a drag would. One chord, one meaning
     * per kind of desktop: pixels where the user places, order where the
     * layout does.
     */
    if (!v->floating && v->workspace &&
        v->workspace->layout != LAYOUT_FLOATING) {
        /* One dimension, so both axes fold onto it. Left/Up move earlier in
         * ws->windows, Right/Down later; every layout re-derives its geometry
         * from that order. */
        layout_move_in_stack(s, v, (dx + dy) < 0 ? -1 : 1);
        return;
    }

    /*
     * Everything below is the keyboard's version of what
     * grab_release_constraints() does for a pointer grab, and it is written out
     * again rather than called because that function re-centres a restored
     * window ON THE CURSOR — correct when the hand is holding the titlebar,
     * badly wrong here, where it would teleport the window to wherever the
     * pointer happens to be sitting before moving it a single step.
     */
    if (v->maximized) view_apply_maximized(s, v, 0);
    if (v->snapped)   snap_release_view(s, v, 1);

    /* The user has placed this window himself now, exactly as a drag would
     * mean, so layout_float_arrange must step over it forever after. Without
     * this the very next reflow on a floating desktop sweeps the window back
     * into its grid cell and the move looks like it never happened. */
    v->hand_placed = 1;

    int nx = v->x + dx * WINDOW_MOVE_STEP;
    int ny = v->y + dy * WINDOW_MOVE_STEP;

    struct wlr_box area;
    if (v->output) output_usable_box_of(s, v->output, &area);
    else           server_usable_box(s, &area);

    /* Clamp against the window's own size, so the limit is "this much of it is
     * still on screen" rather than "its corner is". A window wider than the
     * output still moves: the low bound is below the high one either way. */
    int keep_x = (v->w < WINDOW_MOVE_KEEP_VIS) ? v->w : WINDOW_MOVE_KEEP_VIS;
    int keep_y = (v->h < WINDOW_MOVE_KEEP_VIS) ? v->h : WINDOW_MOVE_KEEP_VIS;

    int min_x = area.x - v->w + keep_x, max_x = area.x + area.width  - keep_x;
    int min_y = area.y,                 max_y = area.y + area.height - keep_y;

    if (nx < min_x) nx = min_x;
    if (nx > max_x) nx = max_x;
    /* The top edge clamps to the usable box itself, not to a sliver: a
     * titlebar dragged above the bar is a window you cannot grab again. */
    if (ny < min_y) ny = min_y;
    if (ny > max_y) ny = max_y;

    view_move(v, nx, ny);

    /* Tell an X11 client where it ended up. view_move only moves the scene
     * node, and an X client learns its root position from a ConfigureNotify and
     * nowhere else — leave it stale and the window's own menus open over the
     * place it was moved from, which is the Steam bug the drag-release path
     * documents. Idempotent, so re-sending the same box costs nothing. */
    if (v->is_xwayland && v->mapped)
        view_resize(v, v->x, v->y, v->w, v->h);
}

/*
 * ⛔ "OPEN THE TASK MANAGER" IS NOT "TOGGLE THE TASK MANAGER", and until this
 * table existed there was no way to say the first one.
 *
 * Every panel below is reached by ONE action that toggles, which is exactly
 * right for the key that opens it — Ctrl+Alt+Del is a switch and a second
 * press should put it away. It is exactly wrong for anything acting on a
 * sentence. The assistant resolves "open the task manager" to
 * `synctl dispatch taskmgr`, and if the panel was already up that CLOSES it
 * and reports "Opened the task manager." — a request that did the opposite of
 * what it said and then claimed it had not. Reported 2026-08-28.
 *
 * ⚠ A NEW VERB, NOT A NEW ARGUMENT. `arg` is already spoken for on half of
 * these — `control <category>`, `theme <preset|#rrggbb…>`, `wallpaper <path>`
 * — so "show" as an argument would be ambiguous with a category or a preset
 * actually called that. `show`/`hide` take the PANEL as their argument
 * instead, which leaves every existing action and every bound key untouched.
 *
 * ⚠ THE NAMES ARE THE DISPATCH NAMES, not the C prefixes. `control` is
 * ctlpanel, `displays` is dispcfg, `bluetooth` is bt, `wallpaper` is wppick —
 * the caller says what it means to open, and this is the one place that has to
 * know the two vocabularies differ.
 */
struct syn_panel_verb {
    const char *name;
    void (*show)(syn_server_t *);
    void (*hide)(syn_server_t *);
};

static const struct syn_panel_verb SYN_PANEL_VERBS[] = {
    { "taskmgr",   taskmgr_show,   taskmgr_hide   },
    { "control",   ctlpanel_show,  ctlpanel_hide  },
    { "calc",      calc_show,      calc_hide      },
    { "clipboard", clipboard_show, clipboard_hide },
    { "emoji",     emoji_show,     emoji_hide     },
    { "news",      news_show,      news_hide      },
    { "keys",      keys_show,      keys_hide      },
    { "clock",     clock_show,     clock_hide     },
    { "calendar",  calendar_show,  calendar_hide  },
    { "displays",  dispcfg_show,   dispcfg_hide   },
    { "bluetooth", bt_show,        bt_hide        },
    { "sounds",    sound_show,     sound_hide     },
    { "filters",   filters_show,   filters_hide   },
    { "theme",     theme_show,     theme_hide     },
    { "widgets",   widgets_show,   widgets_hide   },
    { "wallpaper", wppick_show,    wppick_hide    },
    { "aimodel",   aimodel_show,   aimodel_hide   },
    { "power",     power_show,     power_hide     },
    { "saver",     saver_show,     saver_hide     },
    { "eq",        eq_show,        eq_hide        },
    { "overview",  overview_show,  overview_hide  },
    { "appgrid",   appgrid_show,   appgrid_hide   },
};

/* True when `arg` named a panel this build has. A name it does not know is a
 * refusal, not a silent no-op: synctl reports it and the caller can say so. */
static bool panel_verb(syn_server_t *s, const char *arg, bool show)
{
    if (!arg || !*arg) return false;
    for (size_t i = 0; i < sizeof(SYN_PANEL_VERBS) / sizeof(*SYN_PANEL_VERBS); i++) {
        if (strcmp(arg, SYN_PANEL_VERBS[i].name) != 0) continue;
        if (show) SYN_PANEL_VERBS[i].show(s);
        else      SYN_PANEL_VERBS[i].hide(s);
        return true;
    }
    return false;
}

/* Execute a bind action (see config.c for the names and defaults). */
bool synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    syn_workspace_t *ws = server_active_workspace(s);

    /* ⚠ BEFORE THE CHAIN, because these two are the only actions whose
     * argument is another action's name. See SYN_PANEL_VERBS above. */
    if (strcmp(action, "show") == 0 || strcmp(action, "hide") == 0)
        return panel_verb(s, arg, action[0] == 's');

    if (strcmp(action, "spawn") == 0) {
        spawn(arg);
    } else if (strcmp(action, "spawn_toggle") == 0) {
        synui_spawn_toggle(arg);
    } else if (strcmp(action, "term") == 0) {
        /* The fallback chain, and the reason it tests what is INSTALLED rather
         * than what exited zero, are in synui_terminal_cmd() — a policy
         * decision, spelt where the rest of them are, and testable without
         * linking this file. */
        char cmd[192];
        synui_terminal_cmd(&s->config, cmd, sizeof(cmd));
        spawn(cmd);
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
    } else if (strcmp(action, "focus_app") == 0 ||
               strcmp(action, "close_app") == 0) {
        /*
         * Focus or close a window BY APP-ID, rather than whichever one happens
         * to be focused.
         *
         * Every other window action here aims at `focused_view`, which is
         * right for a keybind and useless to a launcher. Big screen mode has
         * to switch to the browser it started three tiles ago, and on a
         * television there is no click to focus it with first — the pointer
         * is a stick, and pointing it at a window you cannot see is not a
         * gesture anybody can make.
         *
         * wlr-foreign-toplevel-management is the protocol for this and
         * foreign_toplevel.c already serves it — but reaching it needs a
         * Wayland client and the wlr XML is not on a stock SynapseOS box
         * (nothing installs wlr-protocols), so the launcher would have to
         * vendor a protocol to ask a question `synctl dispatch` can carry in
         * one line. These two are that line.
         *
         * ⚠ The FIRST mapped view with that app-id wins, in workspace order.
         * Two windows of one application are deliberately not told apart:
         * from four metres "switch to the browser" is the whole of the
         * intent, and anything finer would need a window id that a launcher
         * would have had to be holding since before the window existed.
         */
        if (arg && *arg) {
            bool closing = strcmp(action, "close_app") == 0;
            syn_view_t *v, *hit = NULL;

            for (int w = 0; w < WORKSPACE_MAX && !hit; w++) {
                wl_list_for_each(v, &s->workspaces[w].windows, link) {
                    const char *id = view_app_id(v);
                    if (v->mapped && id && strcmp(id, arg) == 0) {
                        hit = v;
                        break;
                    }
                }
            }

            if (!hit) {
                /* Not an error: the window may have closed between whoever
                 * listed it and this arriving. Saying so is what stops a
                 * launcher reporting a silent success it did not have. */
                wlr_log(WLR_INFO, "synui: %s — no window with app-id '%s'",
                        action, arg);
            } else if (closing) {
                view_close(hit);
            } else if (!s->locked) {
                /* The same three steps ft_handle_activate takes, and for the
                 * same reason: a window that is minimized, or on a workspace
                 * that is not showing, is still the window somebody asked
                 * for. Focusing it without these two lines "works" and leaves
                 * the screen exactly as it was. */
                if (hit->minimized)
                    view_apply_minimized(s, hit, 0);
                if (hit->workspace && !view_workspace_shown(hit))
                    workspace_switch_on(s, hit->output, hit->workspace->index);
                focus_view(s, hit, view_surface(hit));
            }
        }
    } else if (strcmp(action, "kbd_layout") == 0) {
        /* ⚠ NOT `layout_cycle`, which is the TILING layout. Two things on this
         * desktop are called a layout and only one of them is a keyboard; the
         * action names have to say which, because a bind that retiled the
         * screen when you meant to switch to Norwegian is a bug you would blame
         * on the wrong subsystem.
         *
         * `next` (or no argument) walks forwards, `prev` backwards, and a name
         * or an index goes straight to one — the same vocabulary
         * `synctl layout` takes, so the bind and the CLI cannot come to mean
         * different things. */
        if (arg && strcmp(arg, "prev") == 0) {
            kbd_layout_cycle(s, -1);
        } else if (arg && *arg && strcmp(arg, "next") != 0) {
            int idx = kbd_layout_from_name(s, arg);
            if (idx >= 0) kbd_layout_set(s, idx);
            else wlr_log(WLR_ERROR, "synui: kbd_layout: no layout '%s' in the "
                         "keymap — xkb_layout names %d", arg, kbd_layout_count(s));
        } else {
            kbd_layout_cycle(s, +1);
        }
        /* Say so on screen: on a desktop with nothing focused, switching layout
         * changes no pixels at all, and a bind with no visible effect reads as
         * a dead key. Replaces its own toast rather than stacking one per
         * press, as layout_cycle's does. */
        {
            char lab[64];
            kbd_layout_label(s, kbd_layout_active(s), lab, sizeof(lab));
            s->kbd_layout_notif_id =
                notif_post(s, "synui", "Keyboard layout", lab,
                           NOTIF_URGENCY_LOW, 1200, s->kbd_layout_notif_id);
        }
    } else if (strcmp(action, "layout_cycle") == 0) {
        ws->layout = (ws->layout + 1) % SYN_LAYOUT_COUNT;
        wlr_log(WLR_INFO, "synui: layout → %s", layout_label(ws->layout));
        /* Before the reflow, not after: the choice is the thing worth keeping,
         * and it survives even if placing the windows goes wrong. */
        layout_state_save(s);

        /* Choosing a layout that places windows means "place these windows".
         * Without this the tiler inherits whatever the session floated —
         * a drag, a snap, a maximize — and lays out an empty set, which reads
         * as a tiler that has stopped working (see layout_reclaim).
         *
         * Only the layouts that own their windows' geometry, the same set
         * layout_restore_geometry tests. Floating is where a window is meant to
         * be free, and monocle deliberately keeps honouring a saved float. */
        if (ws->layout == LAYOUT_TILING  || ws->layout == LAYOUT_SPIRAL ||
            ws->layout == LAYOUT_NIRI    || ws->layout == LAYOUT_AI     ||
            ws->layout == LAYOUT_CASCADE)
            layout_reclaim(s, ws);

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
        s->layout_notif_id = notif_post(s, "synui", layout_label(ws->layout), lbody,
                                        NOTIF_URGENCY_LOW, 1500,
                                        s->layout_notif_id);
        /* The control panel's Layout row reads this workspace's layout, and
         * ctlpanel_key() lets Super+Tab through to get here — so with the panel
         * open the row would sit there showing the layout we just left. */
        ctlpanel_refresh(s);
    } else if (strcmp(action, "retile") == 0) {
        /* "TILE THIS DESKTOP, NOW." The key is called tile, so it tiles — it
         * switches from ANY layout, exactly like `cascade` below, and it always
         * re-lays the desktop out.
         *
         * It used to switch only from FLOATING, on the reasoning that the other
         * five place windows already so there was nothing to do but reclaim.
         * That reasoning is wrong from the keyboard: on a cascade or a monocle
         * desktop the key changed nothing you could see and then said "every
         * window was already tiled" — a report that is true of the window flags
         * and a lie about the screen, which is still showing overlapping cards.
         * (velle, 2026-08-07, with a screenshot of exactly that.) Tiling is a
         * destination, not a modifier on the layout you happen to be in.
         *
         * The reclaim is still what does the work: it hands back every window
         * that had been dragged, snapped, floated or maximized out of the flow.
         * layout_apply then runs unconditionally — a re-tile with nothing to
         * reclaim is not a no-op, it is the re-tile. */
        int switched = 0;
        if (ws->layout != LAYOUT_TILING) {
            ws->layout = LAYOUT_TILING;
            switched = 1;
            layout_state_save(s);   /* same rule as layout_cycle: it's a choice */
        }
        int taken = layout_reclaim(s, ws);
        layout_apply(s, ws);

        /* Always says something, and never says "nothing happened": the windows
         * were just laid out again whether or not any of them had to be taken
         * back first. Silence, and then a report of a no-op, is what made this
         * key confusing twice over. */
        char rbody[96];
        if (taken)
            snprintf(rbody, sizeof(rbody), "Desktop %d — %d window%s back in the layout",
                     ws->index + 1, taken, taken == 1 ? "" : "s");
        else
            snprintf(rbody, sizeof(rbody), "Desktop %d — laid out again",
                     ws->index + 1);
        s->layout_notif_id = notif_post(s, "synui",
                                        switched ? "tiling" : "Re-tiled", rbody,
                                        NOTIF_URGENCY_LOW, 1500,
                                        s->layout_notif_id);
    } else if (strcmp(action, "cascade") == 0) {
        /* "Deal this desktop out." Same shape as `retile` above — switch from
         * whatever layout you are on, reclaim, re-lay out, say what happened —
         * and split out rather than folded into it with an argument, because
         * the two answer different questions and a user reading a bind list
         * should see both. Reaching either one by cycling walks the desk
         * through up to six other arrangements on the way. */
        int switched = 0;
        if (ws->layout != LAYOUT_CASCADE) {
            ws->layout = LAYOUT_CASCADE;
            switched = 1;
            layout_state_save(s);   /* same rule as layout_cycle: it's a choice */
        }
        int taken = layout_reclaim(s, ws);
        layout_apply(s, ws);

        char cbody[96];
        if (taken)
            snprintf(cbody, sizeof(cbody), "Desktop %d — %d window%s back in the layout",
                     ws->index + 1, taken, taken == 1 ? "" : "s");
        else
            snprintf(cbody, sizeof(cbody), "Desktop %d — dealt out again",
                     ws->index + 1);
        s->layout_notif_id = notif_post(s, "synui",
                                        switched ? "cascade" : "Re-dealt", cbody,
                                        NOTIF_URGENCY_LOW, 1500,
                                        s->layout_notif_id);
    } else if (strcmp(action, "float_arrange") == 0) {
        /* "Tidy this desk." Clears every hand placement on the desktop and lays
         * the floating windows back out into the inset grid.
         *
         * The counterpart to `retile`, and deliberately NOT the same key: retile
         * switches a floating desktop TO tiling, which is the opposite of what
         * someone who likes floating wants. This one keeps you on the layout you
         * chose and only undoes the dragging.
         *
         * Works from any layout — on the other five hand_placed is a field
         * nothing reads, so the clear is harmless and the reflow is the one they
         * would have done anyway. Saying so is better than a key that reports
         * "not on this layout": the desktop still ends up tidy. */
        int freed = layout_float_release_all(s, ws);

        char fbody[96];
        if (ws->layout != LAYOUT_FLOATING)
            snprintf(fbody, sizeof(fbody), "Desktop %d is on %s — re-laid out",
                     ws->index + 1, layout_label(ws->layout));
        else if (freed)
            snprintf(fbody, sizeof(fbody), "%d hand-placed window%s back in the grid",
                     freed, freed == 1 ? "" : "s");
        else
            snprintf(fbody, sizeof(fbody), "Desktop %d — nothing was out of place",
                     ws->index + 1);
        s->layout_notif_id = notif_post(s, "synui", "Arranged", fbody,
                                        NOTIF_URGENCY_LOW, 1500,
                                        s->layout_notif_id);
    } else if (strcmp(action, "master_shrink") == 0) {
        layout_adjust_master(s, ws, -0.05f);
    } else if (strcmp(action, "master_grow") == 0) {
        layout_adjust_master(s, ws, +0.05f);
    } else if (strcmp(action, "column_consume") == 0) {
        /* niri desktops only; layout_column_join returns without reflowing on
         * every other layout, so the keys are dead rather than surprising. */
        layout_column_join(s, s->focused_view, 1);
    } else if (strcmp(action, "column_expel") == 0) {
        layout_column_join(s, s->focused_view, 0);
    } else if (strcmp(action, "focus_next") == 0) {
        focus_next(s, 1);
    } else if (strcmp(action, "focus_prev") == 0) {
        focus_next(s, -1);
    } else if (strcmp(action, "alt_tab") == 0) {
        /* Two switchers behind one key, picked by `alt_tab_style` — mission
         * control by default. See config.alt_tab_overview: the overview is
         * the whole desk at a size you can see, the strip is MRU order. The
         * gesture is identical either way (hold Alt, tap Tab, let go), which
         * is what lets the setting be a preference rather than a relearn. */
        if (s->config.alt_tab_overview) overview_alt_step(s, 1);
        else                            alttab_step(s, 1);
    } else if (strcmp(action, "alt_tab_prev") == 0) {
        if (s->config.alt_tab_overview) overview_alt_step(s, -1);
        else                            alttab_step(s, -1);
    } else if (strcmp(action, "alt_tab_commit") == 0) {
        /* What letting go of Alt does. Bound to no key — a real cycle ends on a
         * modifier release, which only keyboard_handle_modifiers can see.
         *
         * It exists because everything the cycle DOES now happens here rather
         * than on the way past (switch desktop, restore a minimized window),
         * and a modifier release cannot be synthesised into a headless synui:
         * the headless backend has no input devices, and uinput would be
         * delivered to the live session instead. Same seam as
         * SYNUI_POWER_SUPPLY_DIR in the lid tests.
         *
         * Both switchers, unconditionally rather than by `alt_tab_style`:
         * each is a no-op unless its own cycle is up, and the style can be
         * changed by a SIGHUP reload in the middle of one. */
        alttab_finish(s);
        overview_alt_commit(s);
    } else if (strcmp(action, "stack_next") == 0) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, 1);
    } else if (strcmp(action, "stack_prev") == 0) {
        if (s->focused_view) layout_move_in_stack(s, s->focused_view, -1);
    } else if (strcmp(action, "move_left")  == 0 ||
               strcmp(action, "move_right") == 0 ||
               strcmp(action, "move_up")    == 0 ||
               strcmp(action, "move_down")  == 0) {
        /* Matched exactly, never by prefix: "move_output" lives in this same
         * chain and a strncmp(action, "move_", 5) here would swallow it. */
        if (s->focused_view) {
            int dx = 0, dy = 0;
            switch (action[5]) {
            case 'l': dx = -1; break;
            case 'r': dx =  1; break;
            case 'u': dy = -1; break;
            default:  dy =  1; break;
            }
            window_move_key(s, s->focused_view, dx, dy);
        }
    } else if (strcmp(action, "float_toggle") == 0) {
        syn_view_t *v = s->focused_view;
        if (!v) return true;
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
        if (!s->focused_view) return true;
        view_apply_maximized(s, s->focused_view, !s->focused_view->maximized);
    } else if (strcmp(action, "expand_v_toggle") == 0) {
        /* The keyboard half of the border double-click: fill the usable box
         * vertically, or put the height back. Named for the AXIS rather than
         * for an edge because there is no cursor here to have been on one —
         * WLR_EDGE_TOP is simply how view_apply_edge_expand spells "vertical".
         *
         * Worth having on its own: every other window action in synui is
         * reachable from a key, this is the one gesture that would not have
         * been, and it is also what lets the feature be tested without
         * synthesising a double-click into a nested compositor. */
        if (!s->focused_view) return true;
        view_apply_edge_expand(s, s->focused_view, WLR_EDGE_TOP);
    } else if (strcmp(action, "expand_h_toggle") == 0) {
        if (!s->focused_view) return true;
        view_apply_edge_expand(s, s->focused_view, WLR_EDGE_LEFT);
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
    } else if (strcmp(action, "display_mode") == 0) {
        /* Bare cycles Extend → Duplicate → Built-in off, the way a laptop's
         * display key does. With an argument it SETS one, so a script (or a
         * dock's hotplug rule) can ask for a specific arrangement rather than
         * having to know which one is current and count steps to it. */
        if (arg && arg[0]) {
            int m = display_mode_from_name(arg);
            if (m >= 0) dispcfg_set_mode_cfg(s, m);
            else wlr_log(WLR_ERROR, "synui: display_mode: unknown '%s'", arg);
        } else {
            dispcfg_cycle_mode(s);
        }
    } else if (strcmp(action, "display_scale") == 0) {
        /* THE ACCESSIBILITY CONTROL: one number that makes the whole desktop
         * bigger — synui's own panels, every application, the cursor — at full
         * sharpness rather than by magnifying pixels.
         *
         * `+`/`-` step the ladder, a number sets it outright
         * (`display_scale 1.5`), and bare steps up and wraps at the top so a
         * single bind can walk it. Applies to EVERY screen: growing one
         * monitor of three has not made the desktop bigger, it has made the
         * desk inconsistent. Per-monitor scale is `-`/`+` in Super+D.
         *
         * ⚠ NOT font.state's scale, which the control panel's Text scale row
         * writes. That one sizes text inside the suite's own QML windows and
         * cannot touch a cairo panel or Firefox. Both are real settings and
         * neither is the other's spelling. */
        if (arg && (!strcmp(arg, "+") || !strcmp(arg, "up"))) {
            dispcfg_scale_step_all(s, +1);
        } else if (arg && (!strcmp(arg, "-") || !strcmp(arg, "down"))) {
            dispcfg_scale_step_all(s, -1);
        } else if (arg && arg[0]) {
            char *end = NULL;
            float v = strtof(arg, &end);
            if (end && end != arg && v > 0.0f) dispcfg_set_scale_all(s, v);
            else wlr_log(WLR_ERROR, "synui: display_scale: not a scale '%s'",
                         arg);
        } else {
            dispcfg_scale_step_all(s, +1);
        }
    } else if (strcmp(action, "wallpaper") == 0) {
        /* Bare (Super+W, the control panel row) opens the picker. With a path
         * it sets that wallpaper outright, which is what the Antiquity theme
         * picker dispatches so a palette can bring its own wallpaper — its bar
         * is 80% wallpaper by construction, so the two are one choice.
         *
         * The arg is a path and nothing else: no "default"/"matrix"/"none"
         * tokens. Those are picker rows, and a second parser for them here is
         * a copy of wppick's row table that would drift out of step with it. */
        if (arg && *arg) wppick_set_path(s, arg);
        else             wppick_toggle(s);
    } else if (strcmp(action, "wallhaven") == 0) {
        /* Where more wallpapers come from. Not a compositor panel — it is a
         * quickshell surface of its own, for the same reason the welcome guide
         * is: a grid of remote thumbnails means HTTP, JSON and JPEG decoding,
         * and this process is the one place on the machine where a slow DNS
         * lookup is a frozen desktop. weather.c and news.c each pay for their
         * network with a worker thread and a stop flag wired into libcurl's
         * progress callback; a picker does not need to.
         *
         * ⚠ THE LAUNCHER AND NOT quickshell DIRECTLY. synui-wallhaven owns
         * which tree the QML comes from and the toggle across a process
         * boundary; the window itself owns the network switch, and says so on
         * its own face while it is off. The Super+W picker's Wallhaven row and
         * its [w] button make the identical call.
         *
         * ⚠ AND IT NAMES THE FOCUSED OUTPUT, like the welcome guide — without
         * it the browser opens on every monitor at once.
         *
         * ⛔ AND THE PICKER GOES AWAY FIRST. The browser is a focusable
         * full-screen layer surface, so with the picker still up two
         * full-screen surfaces are both asking for the keyboard and neither is
         * drivable — which is exactly what this key did while the picker was
         * open. wppick_hide() on a picker that is not visible is a no-op. */
        if (s->wppick.visible) wppick_hide(s);
        synui_wallhaven_ipc(s, "toggle");
    } else if (strcmp(action, "cursor") == 0) {
        curpick_toggle(s);
    } else if (strcmp(action, "crop") == 0) {
        /* The one action that TAKES an argument. It no longer requires one:
         * without a path the panel opens on its recent-images list, which is
         * what makes it bindable (super+shift+x) rather than reachable only
         * from a file manager that already knows the filename. */
        if (arg && *arg) crop_open(s, arg);
        else             crop_toggle(s);
    } else if (strcmp(action, "view") == 0) {
        /* The image VIEWER — the same panel and the same decoded image as the
         * cropper, showing the picture whole (crop.c). What the Image Viewer
         * menu entry dispatches, and therefore what an image opened from a file
         * manager reaches, so it takes a path the same way. Without one it
         * opens the recent-images list, told to come back to the viewer. */
        if (arg && *arg) crop_view_open(s, arg);
        else             crop_view_toggle(s);
    } else if (strcmp(action, "equalizer") == 0) {
        eq_toggle(s);
    } else if (strcmp(action, "apps") == 0) {
        appgrid_toggle(s);
    } else if (strcmp(action, "apps_rescan") == 0) {
        /* Separate from `apps` on purpose: the scan is once per session, so a
         * thing installed while you were logged in is invisible until something
         * says so, and making the OPEN rescan would put a walk of every XDG
         * data directory on a keypress. */
        appgrid_rescan(s);
        if (s->appgrid.visible) synui_render_appgrid(s);
    } else if (strcmp(action, "emoji") == 0) {
        emoji_toggle(s);
    } else if (strcmp(action, "calc") == 0) {
        calc_toggle(s);
    } else if (strcmp(action, "font") == 0) {
        /* No default keybind: this is a settings panel reached from Control
         * panel ▸ Appearance ▸ UI font, and the bind table is already dense
         * enough that claiming a letter for a font picker would displace
         * something used far more often. The action exists so a user CAN bind
         * it, and so the control-panel row has something to fire. */
        fontpick_toggle(s);
    } else if (strcmp(action, "cursor_reload") == 0) {
        /* What synui-cursor(1) dispatches after writing cursor.state, so a
         * theme installed from a terminal takes effect without a re-login. */
        cursor_reload(s);
    } else if (strcmp(action, "launcher_style") == 0) {
        launcher_toggle_style(s);
    } else if (strcmp(action, "volume") == 0) {
        /* synui-volume(1) rather than wpctl directly, and the difference is the
         * equalizer. It is a virtual sink that FEEDS a real one, so while it is
         * on @DEFAULT_AUDIO_SINK@ is the CHAIN and these keys would drive a
         * second gain stage stacked on the device's own. Two stages multiply:
         * velle's headset at 29% is 0.0244 amplitude, squared is -64.5 dB, and
         * "turning eq on mutes my music through bluetooth" is what that sounds
         * like. The helper resolves the device behind the chain, at press time
         * from the live link, so it stays right when a headset connects while
         * the equalizer is already on. It keeps wpctl's semantics, -l cap and
         * all; with no equalizer running it IS @DEFAULT_AUDIO_SINK@. */
        if (arg && strcmp(arg, "up") == 0)
            spawn("synui-volume up");
        else if (arg && strcmp(arg, "down") == 0)
            spawn("synui-volume down");
        else if (arg && strcmp(arg, "mute") == 0)
            spawn("synui-volume mute");
        else
            wlr_log(WLR_ERROR, "synui: volume: bad arg '%s'", arg ? arg : "");
        /* The feedback blip, off by default like every other event sound. Not
         * on mute: a sound to confirm silence is the one case where the sound
         * is the wrong answer. */
        if (arg && strcmp(arg, "mute") != 0)
            sound_play(s, SOUND_EVT_VOLUME);
    } else if (strcmp(action, "power") == 0) {
        power_toggle(s);
    } else if (strcmp(action, "saver") == 0) {
        saver_toggle(s);
    } else if (strcmp(action, "taskmgr") == 0) {
        taskmgr_toggle(s);
    } else if (strcmp(action, "aimodel") == 0) {
        aimodel_toggle(s);
    } else if (strcmp(action, "news") == 0) {
        news_toggle(s);
    } else if (strcmp(action, "game") == 0) {
        game_toggle(s);
    } else if (strcmp(action, "cat") == 0) {
        cat_toggle(s);
    } else if (strcmp(action, "solid") == 0) {
        /* The one-press way out of every see-through surface — the control
         * panel's "Make it all solid" row is this action, so a key and the row
         * cannot come to mean different things. Idempotent: pressing it on a
         * desktop that is already solid changes nothing and says so. */
        synui_effects_solid(s);
    } else if (strcmp(action, "clear") == 0) {
        /* Its opposite, and reached the same way for the same reason: the
         * control panel's "Make it all clear" row IS this action. Idempotent. */
        synui_effects_clear(s);
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
         * work (rewrite the systemd drop-in, record the backend state,
         * restart synapd); synui just fires it. The welcome guide's "AI Backend"
         * row reflects the new state the next time the menu is opened. */
        spawn("synui-ai-backend toggle");
    } else if (strcmp(action, "network") == 0) {
        /* Wi-Fi / network configuration. NetworkManager's nmtui is the whole
         * UI here — synui has no text entry to type a passphrase into, so a
         * compositor-native picker would need one built first. Reaching it took
         * knowing it was buried in waybar's SYNAPSE menu; this puts it on a
         * keybind and in the welcome guide. Configurable (network_cmd) so a box
         * running iwd rather than NM can point it somewhere else. */
        spawn(s->config.network_cmd);
    } else if (strcmp(action, "wallpaper_reload") == 0) {
        synui_config_reload(s);
    } else if (strcmp(action, "deskicons_refresh") == 0) {
        /* Rescan ~/Desktop.
         *
         * ⚠ There is NO inotify watch on that directory (see the comment above
         * deskicons_reload in deskmenu.c), so a .desktop file written into it
         * by anything else does not appear until something makes the compositor
         * look again — and until this existed the only things that did were
         * toggling desktop icons off and on, arranging them, and a drag-and-
         * drop. So every tool on this system that offers to "put an icon on the
         * desktop" wrote the file correctly and put nothing on screen.
         *
         * A bind action rather than a new IPC verb because every bindable
         * action is already scriptable through `synctl dispatch` — which is how
         * syn-arcade's `fit` calls it — and a second registry is a second thing
         * to keep in step. Cheap, and a no-op while desktop icons are off. */
        deskicons_reload(s);
    } else if (strcmp(action, "font_refresh") == 0) {
        /* Re-read the UI font family from font.state and repaint.
         *
         * ⚠ There is NO inotify watch on that file either, and it is written
         * by synui-apply-font(1) on behalf of the WHOLE suite — synfiles,
         * syn-settings and syn-disks all offer a font row. Until this existed
         * synui kept its own `ui_font` copy, so a font changed from any of
         * those moved every application on the desktop and left the
         * compositor's own panels — dock, control panel, notifications, window
         * titles — drawing in the previous face until the next login.
         *
         * The script dispatches this as its last act, after font.state is on
         * disk, so a repaint here always reads the new family. Same shape as
         * deskicons_refresh above, and for the same reason: a bind action is
         * already scriptable through `synctl dispatch`, and a second registry
         * is a second thing to keep in step. */
        fontpick_refresh(s);
    } else if (strcmp(action, "widgets") == 0) {
        /* Super+Shift+A: the widget manager, one row per widget. It replaced a
         * blind group toggle, exactly as the filter panel replaced one — and,
         * as there, the old behaviour is still one key: Space from any row.
         * `spawn synui-widgets toggle` remains bindable for anyone who wants
         * the panel-less version back. */
        widgets_toggle(s);
    } else if (strcmp(action, "mixer") == 0) {
        /* The volume mixer, which lives in the BAR — outputs, inputs and a
         * slider per application stream (quickshell/components/Mixer.qml).
         *
         * ⚠ IT IS NOT `sounds`. That is synui's own Event sounds panel, and
         * synui-sound.desktop — the menu's "Volume Mixer" — dispatched it,
         * because there was no way to ask the bar for the real mixer and
         * `sounds` was the nearest thing that existed. A dead entry had been
         * "fixed" into a wrong one.
         *
         * Same route as the start menu: quickshell's IPC goes client-ward,
         * which is the direction synui cannot go on its own. See
         * synui_start_menu_open() for why the output is passed in. */
        synui_bar_ipc(s, "mixer", "toggle");
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
        /* The guide's "Don't show again" checkbox, bottom-left. Persisted, so
         * the choice survives the restart it is about. Note the sense: the
         * checkbox is the opt-OUT and this field is the opt-IN, and
         * quickshell/welcome/GuideState.qml is the only place that inverts it.
         *
         * ⚠ NOTHING IS REDRAWN HERE, AND THE BOX STILL TICKS. The guide watches
         * welcome.state, so the write below IS the update — one writer (synui
         * owns its own config) and one reader, rather than a compositor and a
         * client each holding an opinion about a checkbox. */
        s->config.welcome_at_startup = !s->config.welcome_at_startup;
        welcome_state_save(&s->config);
    } else if (strcmp(action, "menu") == 0) {
        /* Toggling is synui-welcome(1)'s job, not a flag here: closing the
         * guide ENDS its process, so "closed" and "not running" are the same
         * state and the script asks a running instance first, starting one only
         * when nothing answers. A `visible` bool on this side would be a second
         * opinion about a process this one does not own.
         *
         * The focused output is passed because synui is the only thing that
         * knows it — no Wayland protocol tells a layer-shell client which
         * monitor has focus — and it is answering a keypress it just handled. */
        synui_welcome_ipc(s, "toggle");
    } else if (strcmp(action, "control") == 0) {
        /* Bare (Super+C) toggles the front door; with a category name it opens
         * onto that category, which is what the start menu's Settings submenu
         * dispatches — see ctlpanel_show_cat. */
        if (arg && *arg) ctlpanel_show_cat(s, arg);
        else             ctlpanel_toggle(s);
    } else if (strcmp(action, "keys") == 0) {
        /* Super+/ — the bind table as a searchable palette, and the rebind
         * editor (F2 on a row). See keys.c. */
        keys_toggle(s);
    } else if (strcmp(action, "overview") == 0) {
        /* Super+X — mission control. X for eXposé, which is what this feature
         * was called before it was called Mission Control, and the letter was
         * free where every word in "overview", "mission" and "windows" was
         * already taken twice over.
         *
         * Deliberately NOT super+tab, which is the key GNOME and Windows use
         * for their task view: that is layout_cycle here, and silently moving
         * a shortcut somebody has in their fingers to make room for a new
         * feature is exactly the kind of change that should be asked about
         * rather than shipped. Both are one F2 away in the palette. */
        overview_toggle(s);
    } else if (strcmp(action, "theme") == 0) {
        /* Bare (Super+T, the control panel row) opens the picker. With an
         * argument it applies one outright — either a preset token ("dark") or
         * three #rrggbb colours (accent, panel surface, ink), which is what the
         * Antiquity bar's theme picker dispatches so its palette reaches the
         * surfaces synui draws itself. Same shape as `wallpaper` above, and for
         * the same reason: one concept, one bindable name. */
        if (arg && *arg) theme_dispatch(s, arg);
        else             theme_toggle(s);
    } else if (strcmp(action, "bluetooth") == 0) {
        bt_toggle(s);
    } else if (strcmp(action, "printers") == 0) {
        /* cups ships a complete admin UI on localhost:631; there is no GUI to
         * write. Opening the page is also what starts cupsd — cups.socket has a
         * loopback TCP listener (a drop-in the installer ships), so the URL
         * socket-activates the daemon rather than refusing the connection. The
         * control panel's Printers row and the start menu both land here. */
        synui_spawn("xdg-open http://localhost:631/");
    } else if (strcmp(action, "printers_scan") == 0) {
        /* Find network printers and set them up, driverless. The `printers`
         * action above opens CUPS's admin page, which is the right place to go
         * when something needs configuring and the wrong place to start: it
         * asks for a discovery protocol and a driver, and the answer to both
         * for anything sold this decade is "ask the printer". synui-printers
         * does that and reports by toast, because launched from a menu or a
         * control-panel row there is no terminal for it to print to. */
        synui_spawn("synui-printers add --auto --notify");
    } else if (strcmp(action, "settings") == 0) {
        /* syn-settings — the settings APP, which is a normal program and not a
         * compositor panel. The two are not rivals: the control panel is what
         * the DESKTOP is set to (a panel can only configure the compositor
         * drawing it), while syn-settings is what the SYSTEM is set to — the
         * clock, the locale, the kernel, the default applications — and has to
         * work where synui is not running at all. That is why the AI backend
         * lives there and not here. Rows in both that lead to the other are the
         * whole point: neither is findable from inside the other otherwise.
         *
         * The argument names a pane (Display ▸ Monitor settings passes
         * `display`), and it is CHECKED AGAINST A LIST rather than quoted.
         * synui_spawn() runs /bin/sh -c and this action is reachable from the
         * control socket, so an unchecked argument is a command line anybody
         * who can reach the socket gets to write. A whitelist is also the
         * honest shape here: the panes are a fixed set that syn-settings itself
         * enumerates, not free text. */
        static const char *const panes[] = {
            "display", "region", "time", "network", "bluetooth",
            "power", "apps", "kernel", "ai", "system",
        };
        const char *pane = NULL;
        if (arg && *arg) {
            for (size_t i = 0; i < sizeof(panes) / sizeof(panes[0]); i++)
                if (strcmp(arg, panes[i]) == 0) { pane = panes[i]; break; }
            if (!pane)
                wlr_log(WLR_ERROR, "synui: settings: unknown pane '%s'", arg);
        }

        char cmd[128];
        if (pane) snprintf(cmd, sizeof(cmd), "syn-settings gui %s", pane);
        else      snprintf(cmd, sizeof(cmd), "syn-settings gui");
        synui_spawn(cmd);
    } else if (strcmp(action, "about") == 0) {
        /* "About OS" — areofyl/fetch in a terminal (about_cmd). Not a native
         * panel: everything an About box would show, fetch already gathers, and
         * a compositor-drawn one would be a second source of truth about the
         * machine that could disagree with `syn info`.
         *
         * It reports the desktop's own theme, wallpaper and cursor, which is
         * why those three are worth the row at all — they are the part of an
         * About box that is about THIS desktop rather than about the hardware. */
        spawn(s->config.about_cmd);
    } else if (strcmp(action, "clock") == 0) {
        /* "Date & Time" — the compositor's clock/time settings panel. */
        clock_toggle(s);
    } else if (strcmp(action, "calendar") == 0) {
        /* Super+Shift+T, and the bar clock's on-click (via synctl dispatch). */
        calendar_toggle(s);
    } else if (strcmp(action, "night_light") == 0) {
        nightlight_toggle(s);
    } else if (strcmp(action, "dnd") == 0) {
        /* Do Not Disturb — no toast, no chime. Its own confirmation is the one
         * thing that comes through, so the key never looks dead. */
        notif_dnd_toggle(s);
    } else if (strcmp(action, "record") == 0) {
        /* Record the monitor the user is actually on — same reason screenshot
         * does this above. wf-recorder captures one output and, given no name
         * on a multi-monitor layout, prompts on stdin for a menu number; the
         * keybind's child has no terminal, so that read hits EOF and it dies
         * before recording anything. Only the compositor knows the focus. */
        syn_output_t *o = server_focused_output(s);
        const char *name = (o && o->wlr_output) ? o->wlr_output->name : NULL;
        /* --audio only when the control panel's Sound ▸ Record audio row says
         * so. The script turns that into the default sink's monitor — desktop
         * sound, never the mic (wf-recorder's bare -a would be the mic, which
         * is why synui-record resolves the device itself). Read here rather
         * than baked into a second keybind: one bind starts and stops one
         * recorder, and pkill would not know which of two started it. */
        const char *aud = s->config.record_audio ? " --audio" : "";
        /* --edit when Sound ▸ Record for editing is on: synui-record then
         * captures DNxHR in a .mov instead of the H.264 mp4. Independent of
         * --audio rather than exclusive with it — the mezzanine carries PCM,
         * which is the other half of what a free Resolve on Linux can read. */
        const char *ed = s->config.record_edit ? " --edit" : "";
        char cmd[256];
        if (name && *name)
            snprintf(cmd, sizeof(cmd), "synui-record --output '%s'%s%s",
                     name, aud, ed);
        else
            snprintf(cmd, sizeof(cmd), "synui-record%s%s", aud, ed);
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
        if (!v || !v->mapped) return true;
        syn_output_t *cur = v->output ? v->output : server_focused_output(s);
        if (!cur) return true;
        bool prev = arg && strcmp(arg, "prev") == 0;
        /* Step one element in the wl_list, wrapping past the head sentinel. */
        struct wl_list *node = prev ? cur->link.prev : cur->link.next;
        if (node == &s->outputs) node = prev ? s->outputs.prev : s->outputs.next;
        syn_output_t *next = wl_container_of(node, next, link);
        if (!next || next == cur) return true;                  /* only one monitor */

        struct wlr_box from, to;
        output_box_of(s, cur,  &from);
        output_box_of(s, next, &to);

        /* ⛔ `v->floating` IS THE WINDOW'S FLAG, NOT THE DESKTOP'S, and that is
         * the difference between this key working and looking dead.
         *
         * On a FLOATING desktop no window is marked floating — the desktop is —
         * so a window there took the branch below that places nothing and left
         * the rest to layout_apply. But LAYOUT_FLOATING's pass is
         * layout_float_arrange, which deliberately steps over every window the
         * user has ever dragged or resized (hand_placed). So view_set_output
         * moved the RECORD and left the pixels exactly where they were: the key
         * did nothing visible, and pressing it again only walked the window's
         * idea of which monitor it was on round the ring, out of step with what
         * was on screen.
         *
         * Anything no layout is going to place has to be carried across here. */
        bool placed_by_layout = !v->floating && v->workspace &&
                                (v->workspace->layout != LAYOUT_FLOATING ||
                                 !v->hand_placed);

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
        } else if (!placed_by_layout) {
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
        return false;
    }
    return true;
}

/* ── The welcome guide (Super+Escape) ───────────────────────
 *
 * ⚠ THERE IS NOTHING TO NAVIGATE HERE ANY MORE. The menu was compositor-drawn
 * and this file carried both halves of its input — arrow keys, Enter, the
 * click-off, the corner X, the wheel — roughly 150 lines that existed only
 * because synui was the thing drawing it.
 *
 * It is quickshell/welcome.qml now, a paged guide in its own process, and
 * layer_surface_map() hands a focusable layer surface the keyboard. So the keys
 * are its own and synui gives them up the way it does for every other client —
 * including to the next window that maps, since focus_view() notifies the new
 * toplevel unconditionally. The `menu` action below is the whole of what is left: ask
 * synui-welcome(1) to toggle it, naming the focused output.
 *
 * synui still OWNS the keystroke — handle_keybinding() runs before the focused
 * surface sees anything — so Super+Escape keeps working regardless of what has
 * focus. It just no longer draws the result. Same trade the start menu made.
 */

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

/* Print, for the same reason as the volume keys and then one more.
 *
 * The modal panels absorb every keystroke — aimodel_key ignores its `mods`
 * argument entirely and returns 1 on the way out of its switch — so with a
 * panel open the screenshot binds never ran. That makes a panel the one thing
 * on this desktop that CANNOT be photographed, which is exactly backwards: a
 * menu is where you most want a screenshot, because reporting a layout bug in
 * one is otherwise a description rather than a picture. velle asked for this
 * after hitting precisely that trying to show me the AI panel.
 *
 * Safe ahead of the panels in a way a letter key would not be: Print is not a
 * text character, so it cannot be swallowed from a search box, and no panel
 * binds it. Dispatched with the REAL modifiers so shift+print (region) and
 * ctrl+print (full) keep working, not just bare Print.
 */
static bool is_screenshot_key(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Print || sym == XKB_KEY_3270_PrintScreen;
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

    /* The other half of the preview key: the release of the `p` that raised the
     * saver from the Super+Z panel below. It is the tail of a keystroke the
     * user has already spent, not the user arriving, so it must not dismiss —
     * without this the preview lasts about as long as a keypress. Swallowed all
     * the same: the panel absorbed the press, so no client is waiting on it.
     * Before saver_ate_event() and before notify_activity(), either of which
     * would take the saver down. */
    if (s->saver.preview_key) {
        if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED &&
            s->saver.preview_key == event->keycode + 1) {
            s->saver.preview_key = 0;
            return;
        }
        /* Any other PRESS means that release is never coming — focus moved, the
         * keyboard went away, or the user hit something else (which takes the
         * preview down on its own). Disarm, rather than leave a stale keycode
         * that would one day swallow an unrelated release and strand a key
         * down inside a client. */
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
            s->saver.preview_key = 0;
    }

    /* A key that only woke the screensaver is spent waking it. Release events
     * are swallowed too while the saver is still up, so a client cannot see a
     * release whose press it never got. */
    if (saver_ate_event(s)) return;

    notify_activity(s);

    /* Same as the click path: a keystroke says where the user is, and a keybind
     * is how most panels are opened. Asked before the bind runs, so the panel
     * this key is about to open is not yet visible — see
     * server_ui_output_track(). */
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        server_ui_output_track(s);

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
                /* Media keys reach the player, not the password field.
                 *
                 * They are the one class of key that means the same thing on a
                 * locked screen as on an unlocked one — a keyboard's play
                 * button is for the music, and having it type an invisible
                 * character into a password instead is the behaviour that made
                 * "pause what I locked the machine on" mean unlocking it
                 * first. Handled here rather than through bind_dispatch
                 * because compositor binds are disabled while locked by
                 * contract, and this must not become the exception that
                 * re-enables them. */
                for (int i = 0; i < nsyms; i++) {
                    switch (syms[i]) {
                    case XKB_KEY_XF86AudioPlay:
                    case XKB_KEY_XF86AudioPause:
                        mpris_playpause(); return;
                    case XKB_KEY_XF86AudioNext:  mpris_next();      return;
                    case XKB_KEY_XF86AudioPrev:  mpris_previous();  return;
                    default: break;
                    }
                }
                xkb_keysym_t sym = nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol;
                uint32_t cp = nsyms == 1 ? xkb_keysym_to_utf32(sym) : 0;
                lock_handle_key(s, sym, cp, modifiers);
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

        /* Screenshots, ahead of the modal panels, so that an open menu can be
         * captured instead of eating the key. Real modifiers, not 0: the
         * region and full-screen variants are shift+print and ctrl+print. */
        uint32_t shot_mods = modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                                          WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT);
        for (int i = 0; i < nsyms; i++)
            if (is_screenshot_key(syms[i]) && bind_dispatch(s, syms[i], shot_mods))
                return;
    }

    /* Delete on the DESKTOP.
     *
     * Only when nothing holds keyboard focus, which since the desktop click
     * clears it is exactly "the desktop is what you are working in". Without
     * this the key had nowhere to go at all: velle pressed Delete over a
     * selected desktop icon and the file browser behind deleted its own
     * selection instead. Trash, not unlink — see deskicon_trash_selected. */
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
        !s->focused_view && s->deskicon_selected >= 0) {
        for (int i = 0; i < nsyms; i++)
            if (syms[i] == XKB_KEY_Delete || syms[i] == XKB_KEY_KP_Delete) {
                deskicon_trash_selected(s);
                return;
            }
    }

    /* Command bar absorbs all input when open */
    if (s->cmdbar.visible && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++)
            cmdbar_key(s, syms[i]);
        return;
    }

    /* The modifier tap runs a bind action (see syn_server::tap_armed). Which
     * modifier is `tap_key` in synuirc — Super by default, and rebindable from
     * the palette; 0 means no tap at all, and then this whole block is inert.
     * WHAT it runs is `tap_action`, default start_menu, and it goes through
     * synui_binding_execute() so the tap can reach anything a chord can — rofi,
     * the AI command bar, whatever the user pointed it at with F3 in the
     * palette. It used to call synui_start_menu_open() here, which made the
     * start menu the one thing the tap could ever do.
     *
     * That modifier is first and foremost a modifier, so the tap is defined by
     * what did *not* happen: armed on a bare press of it, disarmed by any other
     * key here and by any pointer button in server_cursor_button. Only a
     * release that survives both is a tap.
     *
     * The release is deliberately not swallowed — it falls through to the
     * normal forwarding path below. Returning early here would leave the
     * focused client holding a modifier it never saw released, i.e. a stuck
     * modifier for as long as it keeps focus. */
    uint32_t tap_mod = s->config.tap_mod;
    bool is_tap_key  = false;
    for (int i = 0; i < nsyms; i++)
        if (tap_mod && syn_tap_mod_from_sym(syms[i]) == tap_mod)
            is_tap_key = true;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* The tap key pressed while another modifier is already down is someone
         * building a chord, not tapping. Its own bit is excluded from that test
         * — xkb has it set by the time this press is reported.
         *
         * Not armed at all while a rebind capture is waiting for a key: there
         * the press IS the answer, and arming would open the start menu on the
         * release of the very key just captured. */
        uint32_t others = (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL |
                           WLR_MODIFIER_ALT  | WLR_MODIFIER_SHIFT) & ~tap_mod;
        s->tap_armed = is_tap_key && !(modifiers & others) &&
                       !s->keys.capturing && !s->ctlpanel.sc_capturing;
    } else if (is_tap_key && s->tap_armed) {
        s->tap_armed = 0;
        if (s->config.tap_action[0])
            synui_binding_execute(s, s->config.tap_action, s->config.tap_arg);
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

        /* Image cropper: modal and full-screen. */
        for (int i = 0; i < nsyms; i++)
            if (crop_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Equalizer: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (eq_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* The application grid. Full-screen and modal, like every panel in this
         * chain — its handler swallows everything it does not act on, so a
         * keystroke cannot reach the window it is covering. */
        for (int i = 0; i < nsyms; i++)
            if (appgrid_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Emoji picker. BEFORE the font picker only because the list is
         * walked in the order panels were added; no two are ever open at
         * once, so the order carries no meaning beyond that. */
        for (int i = 0; i < nsyms; i++)
            if (emoji_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* UI font picker: same modal contract again. */
        for (int i = 0; i < nsyms; i++)
            if (fontpick_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Calculator. Modal, and it claims bare Shift because its expression
         * box needs the shifted characters — ( ) * + ^ % are all Shift on a US
         * layout, so a panel that let Shift through would be a calculator that
         * could not multiply. Ctrl+C and Ctrl+V are claimed too (copy the
         * answer, paste a number into the expression); every other Super+… and
         * Ctrl+… still falls through to the bind table. */
        for (int i = 0; i < nsyms; i++)
            if (calc_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Power saving panel: same modal contract as the display panel. */
        for (int i = 0; i < nsyms; i++)
            if (power_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Screensaver panel: same modal contract as the power panel it sits
         * beside. Kept in the order SYN_PANEL_LIST uses for the pointer, which
         * is the closest thing this chain has to a roster. */
        for (int i = 0; i < nsyms; i++)
            if (saver_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) {
            /* If the saver is up now it was `p` that raised it: nothing else on
             * this path shows it, and a saver that had ALREADY been up would
             * have been dismissed by saver_ate_event() before we got here. Mark
             * the keycode so the release still to come passes through without
             * being read as the user arriving. */
            if (saver_active(s)) s->saver.preview_key = event->keycode + 1;
            return;
        }

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

        /* AI model picker: same modal contract. */
        for (int i = 0; i < nsyms; i++)
            if (aimodel_key(s, syms[i], modifiers))
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

        /* Shortcut palette: modal, and the most greedy of them — it is a search
         * box, so it claims every printable key and bare Shift. Only Super and
         * Alt fall through, which is what lets Super+/ close it. AFTER the
         * control panel, since the control panel can be what opened it. */
        for (int i = 0; i < nsyms; i++)
            if (keys_key(s, syms[i], modifiers))
                absorbed = true;
        if (absorbed) return;

        /* Mission control: same modal contract, and after the palette for the
         * same reason the palette is after the control panel — this one is
         * full-screen, so anything it did not let through would be a panel
         * COVERED rather than merely behind it. */
        for (int i = 0; i < nsyms; i++)
            if (overview_key(s, syms[i], modifiers))
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
         * that is handed the keyboard at map (quickshell/StartMenu.qml), so its keys
         * arrive by the ordinary focus path — synui does not have to intercept
         * them, and must not: swallowing them here is what would make it deaf. */

        /* …and none here for the welcome guide either, for exactly the same
         * reason: it is a quickshell layer surface now, and it takes the
         * keyboard the ordinary way. See the welcome-guide block further down. */
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

    /* And for the same reason: set_keymap resets the locked LAYOUT to 0 too,
     * so a keyboard plugged in after the session had switched to the second
     * layout would arrive on the first — two keyboards on one desk typing
     * different letters. Also the path by which the greeter's adopted keymap
     * reaches devices that attached before it was adopted. */
    kbd_layout_apply(s, wlr_kb);
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

/* The virtual POINTER handler is the mirror of this one, and lives further
 * down beside server_new_input — it needs input_dev_track(), which is defined
 * there. */

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

    /* The user has now placed this window himself, and on a floating desktop
     * that is permanent: layout_float_arrange steps over a hand_placed window
     * forever after, so opening a fifth terminal cannot yank the four you
     * arranged back into a grid. This is the one choke point every hand grab
     * passes through — a titlebar drag, a border pull, Super+drag, and a CSD
     * client's own xdg_toplevel.move/.resize (view_begin_interactive) — which
     * is exactly why the flag is set here and nowhere else.
     *
     * Set for a RESIZE as much as a move: "I chose this window's size" is the
     * same statement about the same window. Harmless on the other five layouts,
     * which never read it. Cleared by Super+Shift+G (float_arrange) and by
     * layout_reclaim. */
    view->hand_placed = 1;

    /* And an expanded axis is no longer expanded: the user is about to give
     * this window a size of their own, so the box remembered from before the
     * double-click describes a window that no longer exists.
     *
     * ⚠ Safe HERE and nowhere earlier. This runs on the first real motion of a
     * drag, never on a press — which is exactly what lets the second
     * double-click on a border collapse the axis instead of finding the state
     * already wiped by its own first press. That is why the border grab is
     * armed; see the DECO_BORDER case in cursor_button(). */
    view->expanded = 0;

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

    /* An armed border press is not yet a drag — the mirror of the check in
     * process_cursor_move, and it is here for the same two reasons: a click
     * that merely lands on a border must not resize the window, and a
     * double-click must reach its second press with the window's state intact.
     *
     * ⚠ THE GEOBOX HAS TO BE RE-TAKEN. begin_interactive_armed records it at
     * press time, before grab_release_constraints has run, so for a maximized
     * or tiled window it describes the box the window is about to leave.
     * Resizing from that box makes the window jump the moment the drag starts.
     * grab_x/grab_y need no such fixing: they are the CURSOR's anchor and the
     * cursor did not move. */
    if (s->grab_armed) {
        double px = s->cursor->x - s->grab_press_x;
        double py = s->cursor->y - s->grab_press_y;
        if (px * px + py * py < GRAB_DRAG_SLOP * GRAB_DRAG_SLOP)
            return;

        s->grab_armed = false;
        grab_release_constraints(s, v, SYNUI_CURSOR_RESIZE);
        s->grab_x      = s->cursor->x;
        s->grab_y      = s->cursor->y;
        s->grab_geobox = (struct wlr_box){ v->x, v->y, v->w, v->h };
    }

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

    /* The curve, which is a separate question from the speed above: accel_speed
     * scales whichever curve the device is on, so asking for a faster pointer
     * never turns acceleration on and asking for a slower one never turns it
     * off. DEFAULT touches nothing.
     *
     * The profile has to be checked against the device's own supported set
     * rather than against accel_is_available(): a device can offer a speed and
     * still support only one profile, and libinput answers such a request with
     * UNSUPPORTED rather than by picking something. Asking anyway would be a
     * config line that silently does nothing, which is the failure this reads
     * the mask to avoid. */
    if (cfg->accel_profile != SYN_ACCEL_PROFILE_DEFAULT) {
        enum libinput_config_accel_profile want =
            cfg->accel_profile == SYN_ACCEL_PROFILE_FLAT
                ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
                : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        if (libinput_device_config_accel_get_profiles(li) & want)
            libinput_device_config_accel_set_profile(li, want);
    }
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
     * off the grab surface and even off-screen.
     *
     * A DRAG-AND-DROP IS THE ONE EXCEPTION, and it has to be, because a DnD
     * drag is always button-held: pinning focus here meant the surface under
     * the cursor never got an enter for the whole drag. wlroots delivers the
     * drop through the drag's own pointer grab (wlr_drag.pointer_grab), whose
     * enter handler is what moves drag focus and — for an Xwayland target —
     * what makes xwm send XdndEnter/XdndPosition. notify_enter() is the only
     * thing that calls it, so returning early here meant the grab never heard
     * about the target and every cross-app drop was refused. The drag icon
     * still tracked the cursor (motion positions it directly, off the grab),
     * which made this look like a target-side or MIME problem rather than
     * focus never arriving. Falling through is safe: the icon surface cannot
     * steal the hit test, since start_drag clears its input region. */
    if (s->seat->pointer_state.button_count > 0 && !s->seat->drag) {
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

    /*
     * A CONSTRAINED POINTER IS NOT REBASED. The cursor did not move — that is
     * the entire premise of this function — so a surface appearing over it is
     * the scene changing, not the user pointing somewhere else, and a game
     * that holds the pointer must not lose it to one.
     *
     * It did. Game mode restarts the bar (`game_stop_bar`), every layer
     * surface it maps lands here, and pointer_update_focus() then hands focus
     * to whatever is now topmost — which deactivates the game's constraint.
     * For a ONESHOT constraint that is not a pause: sending deactivated
     * DESTROYS it, so the game never gets the pointer back and the mouse walks
     * out of it for the rest of the session.
     *
     * Only while the constraint's surface still holds pointer focus and is
     * still mapped, so an unmapped or backgrounded client cannot pin the
     * pointer to itself for ever.
     */
    if (s->active_constraint &&
        s->active_constraint->surface->mapped &&
        s->seat->pointer_state.focused_surface == s->active_constraint->surface)
        return;

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
    X(fontpick, fontpick) \
    X(emoji,    emoji)    \
    X(calc,     calc)     \
    X(eq,       eq)       \
    X(crop,     crop)     \
    X(power,    power)    \
    X(saver,    saver)    \
    X(taskmgr,  taskmgr)  \
    X(news,     news)     \
    X(filters,  filters)  \
    X(aimodel,  aimodel)  \
    X(widgets,  widgets)  \
    X(sound,    sound)    \
    X(clock,    clock)    \
    X(calendar, cal)      \
    X(ctlpanel, ctlpanel) \
    X(keys,     keys)     \
    X(overview, overview) \
    X(appgrid,  appgrid)  \
    X(theme,    thememgr) \
    X(clipboard, clipboard)

/* Is any of them open? Asked where there is nothing to hand the event to but it
 * still must not reach the window underneath — a horizontal wheel, and the
 * release of a click the panel already swallowed the press of. */
/* Windowed panels are NOT modal — they are windows. Excluded from the "is a
 * panel eating this" test below, or an open calculator would still swallow the
 * wheel and the stray release for the whole desktop, which is most of what
 * "forces focus" felt like. Named rather than folded into the X macro because
 * only these three have a mode at all. */
static bool panel_mem_is_modal(syn_server_t *s, const char *mem)
{
    if (!strcmp(mem, "calc"))     return !panel_is_windowed(s, SYN_PDRAG_CALC);
    if (!strcmp(mem, "ctlpanel")) return !panel_is_windowed(s, SYN_PDRAG_CTLPANEL);
    if (!strcmp(mem, "taskmgr"))  return !panel_is_windowed(s, SYN_PDRAG_TASKMGR);
    return true;
}

static bool panel_pointer_active(syn_server_t *s)
{
    /* The command bar is not in the list — it has no rows, so no _motion and no
     * _scroll — but it is just as modal, and the two things this answers (swallow
     * the wheel, swallow the stray release) apply to it identically. */
    if (s->cmdbar.visible) return true;
#define X(fn, mem) if (s->mem.visible && panel_mem_is_modal(s, #mem)) return true;
    SYN_PANEL_LIST(X)
#undef X
    return false;
}

/* Is any of them on screen at all? The modality filter above is deliberately
 * NOT applied: server_ui_output_track() is asking "does something already have
 * a monitor it lives on", and a windowed panel has one as much as a modal one
 * does. Declared in synui.h next to the rest of panel.c's contract, defined
 * here because this is where the one roster lives. */
bool panel_any_visible(syn_server_t *s)
{
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

/*
 * A panel that owns the pointer owns the pointer IMAGE too.
 *
 * The motion path returns as soon as a panel takes the event, which means
 * pointer_update_focus() — the only thing that ever puts the arrow back — does
 * not run for as long as the panel is up. So the cursor keeps whatever image
 * the client underneath last asked for, and the client keeps pointer focus and
 * can go on changing it behind the panel.
 *
 * Usually invisible, because the client underneath is showing an arrow anyway.
 * It is very much not invisible when that client had HIDDEN the pointer: kitty
 * hides it while you type (mouse_hide_wait), and so do games, video players and
 * most PDF viewers. Open the control panel over one of those and the mouse is
 * simply gone — you are left with a panel full of clickable rows and no visible
 * pointer to click them with. That is the "mouse disappears in the control
 * panel" report, and it is worst exactly where the panel is most modal.
 *
 * Clearing focus is the load-bearing half. Merely setting our own image would
 * be undone the moment the client set its own again — and per cursor_set_deco()
 * above, a client only re-asserts its cursor on a wl_pointer.enter, so taking
 * focus away now is also what makes it re-set the cursor properly on the way
 * back out.
 *
 * Not done while a button is held: that is an implicit grab, the release still
 * belongs to the surface that took the press, and stealing focus mid-drag is
 * the bug pointer_update_focus() opens by warning about.
 */
static void panel_pointer_claim_cursor(syn_server_t *s)
{
    if (s->seat->pointer_state.button_count > 0) return;

    if (s->seat->pointer_state.focused_surface) {
        wlr_seat_pointer_notify_clear_focus(s->seat);
        /* Focus is where a constraint lives or dies, so constraints.c has to
         * hear about it from HERE too. This function returns before
         * pointer_update_focus() ever runs, so without this call
         * s->active_constraint is left naming a surface that no longer holds
         * pointer focus — constraints_apply_motion() then bails on its own
         * focus check and a locked pointer silently stops being locked. Same
         * shape as the pointer_rebase() case guarded above. */
        constraints_focus_surface(s, NULL);
    }

    /* Also drops any resize arrow a titlebar edge was holding when the panel
     * opened — deco_hover_update() is below the early return too, so nothing
     * else would take it back. */
    s->deco_cursor = NULL;
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
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

/*
 * Is (lx,ly) the desktop — the wallpaper itself, with nothing of ours and
 * nothing of a client's over it?
 *
 * The same question the desktop right-click answers by falling through every
 * other branch of pointer_button(). A drag-and-drop cannot ask it that way: it
 * has to be answered on every motion event, while the drag is still in flight,
 * so the source can be told whether letting go here will do anything.
 */
static bool point_is_desktop(syn_server_t *s, double lx, double ly)
{
    if (s->nlock.active) return false;
    if (panel_pointer_active(s)) return false;
    if (s->deskmenu.visible || s->dockmenu.visible || s->bt.visible) return false;
    if (dock_entry_at(s, lx, ly)) return false;
    if (dock_bar_at(s, lx, ly, NULL)) return false;

    double sx, sy;
    struct wlr_surface *surface = NULL;
    syn_view_t *view = view_at(s, lx, ly, &surface, &sx, &sy);
    return !view && !surface;
}

/* ── Pointer smoothing ───────────────────────────────────── */
/*
 * A low-pass filter over the cursor's own path, for a pointer that is hard to
 * hold still — a low-DPI or worn sensor whose counts rattle, or a hand that
 * does. libinput has no such option, so this is synui's own.
 *
 * The filter is a leaky bucket, not a plain average over the last N reports.
 * Every delta goes into `pend`, and each event pays out a fraction of whatever
 * is standing there; the rest waits for the next one. Nothing is ever thrown
 * away, so the cursor's total travel still equals the hand's — a smoothed
 * pointer lands in the same place, it just takes a few more milliseconds to
 * settle. An average would instead lose the tail of every movement, which is
 * the shape of filter that makes a desktop feel like it is fighting you.
 *
 * ⚠ THE FRACTION IS DERIVED FROM ELAPSED TIME, NOT FIXED PER EVENT. A fixed
 * fraction makes the amount of smoothing depend on the device's report rate:
 * the same number would be a heavy filter on a 125 Hz office mouse and no
 * filter at all on a 1000 Hz gaming one, on the same desktop, from one setting.
 * Solving for the time constant instead means the row means the same thing on
 * both — which is the point, since the mouse this was written for is the slow
 * one and the other pointer on the machine is the fast one.
 */

/* Time constant in ms for a strength of 1..10: how long the cursor takes to
 * cover ~63% of a step change. 5 ms is at the edge of perceptible; 50 ms is as
 * far as this goes, because past it the pointer stops feeling attached. */
static inline double psmooth_tau_ms(int strength)
{
    if (strength < 1)  strength = 1;
    if (strength > 10) strength = 10;
    return strength * 5.0;
}

/* Hand `pend` to the cursor and clear it. Used both by the per-event path and
 * by the flush timer, so "what a settled pointer does" is written once. */
static void psmooth_emit(syn_server_t *s, double x, double y)
{
    s->psmooth.pend_x -= x;
    s->psmooth.pend_y -= y;
    wlr_cursor_move(s->cursor, s->psmooth.pend_dev, x, y);
}

/*
 * The pointer stopped. Apply what is left.
 *
 * Without this the remainder has no event to ride out on: the filter only
 * advances when motion arrives, so a movement that ends would leave the cursor
 * a fraction of its last step short of where it was put, every time. Small,
 * but it is a systematic bias towards where you came from, and it is exactly
 * the "the pointer doesn't go where I aim" complaint smoothing is supposed to
 * fix rather than cause.
 */
static int psmooth_flush(void *data)
{
    syn_server_t *s = data;
    if (!s->psmooth.pend_dev) return 0;
    if (s->psmooth.pend_x == 0.0 && s->psmooth.pend_y == 0.0) return 0;

    psmooth_emit(s, s->psmooth.pend_x, s->psmooth.pend_y);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* The same clamp the motion path applies, for the same reason. This timer
     * is the OTHER way the cursor moves: it fires after the last motion event,
     * so nothing downstream would clamp what it emits, and a game's confinement
     * would leak by exactly the filter's remainder every time the pointer came
     * to rest against an edge. Before pointer_update_focus() below, so focus is
     * computed at the position the cursor actually ends up in. */
    game_confine_cursor(s);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* The cursor moved, so everything that tracks it has to be told — the
     * pointer focus above all. A flush that only nudged the drawn cursor would
     * leave the seat believing the pointer is still where it was a frame ago,
     * which is a click landing on the wrong side of a window edge. */
    wlr_scene_node_set_position(&s->drag_icon_tree->node,
                                (int)s->cursor->x, (int)s->cursor->y);
    if (s->cursor_mode == SYNUI_CURSOR_MOVE)        process_cursor_move(s);
    else if (s->cursor_mode == SYNUI_CURSOR_RESIZE) process_cursor_resize(s);
    else pointer_update_focus(s, (uint32_t)(s->psmooth.last_ms));
    return 0;
}

/* Re-arm the settle timer, creating it on first use like focus_follow's. */
static void psmooth_arm(syn_server_t *s)
{
    if (!s->psmooth.flush_timer) {
        struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
        s->psmooth.flush_timer =
            wl_event_loop_add_timer(loop, psmooth_flush, s);
        if (!s->psmooth.flush_timer) return;   /* no timer: the tail decays */
    }
    /* One frame at 60 Hz. Long enough that it never fires between two reports
     * of a pointer that is still moving (even a 60 Hz device beats it), short
     * enough that the settle is not something you can watch happen. */
    wl_event_source_timer_update(s->psmooth.flush_timer, 16);
}

/* Put the filter back to empty WITHOUT moving the cursor. For the paths where
 * the pending amount has stopped being meaningful rather than having arrived:
 * the setting was turned off, or a constraint took the pointer. Dropping a
 * sub-pixel remainder there is right — applying it would move the cursor at a
 * moment nothing asked it to. */
static void psmooth_reset(syn_server_t *s)
{
    s->psmooth.pend_x = s->psmooth.pend_y = 0.0;
    s->psmooth.pend_dev = NULL;
    s->psmooth.last_ms = 0;
    if (s->psmooth.flush_timer)
        wl_event_source_timer_update(s->psmooth.flush_timer, 0);
}

/*
 * Run the filter over one delta, in place.
 *
 * Called only for relative devices with the setting on and no constraint
 * active — see the call site for why each of those is excluded.
 */
static void psmooth_apply(syn_server_t *s, uint32_t time_msec,
                          struct wlr_input_device *device,
                          double *dx, double *dy)
{
    /* A delta from a different device cannot be pooled with what is pending:
     * wlr_cursor_move maps a relative motion through the device's own output
     * mapping, so the two halves could belong to different screens. Land the
     * old pointer's remainder first, then start clean. */
    if (s->psmooth.pend_dev && s->psmooth.pend_dev != device)
        psmooth_flush(s);

    s->psmooth.pend_dev = device;
    s->psmooth.pend_x += *dx;
    s->psmooth.pend_y += *dy;

    /* Elapsed time since the last smoothed event. The first event of a stroke,
     * a stale gap (the pointer was idle and the timer did not get to run), and
     * a timestamp that went backwards across a wrap all take the same branch:
     * emit everything. A new movement must not be damped by a filter whose
     * state is older than the movement itself. */
    uint32_t prev = s->psmooth.last_ms;
    s->psmooth.last_ms = time_msec;

    double frac = 1.0;
    if (prev != 0 && time_msec >= prev) {
        uint32_t dt = time_msec - prev;
        if (dt <= 200)
            frac = 1.0 - exp(-(double)dt / psmooth_tau_ms(s->config.pointer_smoothing));
    }

    *dx = s->psmooth.pend_x * frac;
    *dy = s->psmooth.pend_y * frac;
    s->psmooth.pend_x -= *dx;
    s->psmooth.pend_y -= *dy;

    psmooth_arm(s);
}

/* Shared relative-motion path: broadcast the raw delta to relative-pointer
 * clients, let an active constraint absorb (locked) or clamp (confined) the
 * move, then move the cursor and update pointer focus. */
static void process_pointer_motion(syn_server_t *s, uint32_t time_msec,
                                   struct wlr_input_device *device,
                                   double dx, double dy,
                                   double unaccel_dx, double unaccel_dy,
                                   bool smoothable)
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

    /* Smoothing goes HERE and not above: everything before this point is the
     * raw stream, and it has to stay raw.
     *
     *  - the relative-pointer broadcast is what a game reads while it holds the
     *    pointer. Filtering a player's aim is not what somebody steadying their
     *    desktop cursor asked for, and it would be invisible to them — the
     *    setting is on the pointer page, the symptom is in the game.
     *  - a constraint has already had its say. A confined pointer's delta was
     *    just clamped to a region; re-filtering it would hand back a value that
     *    is outside the region again.
     *
     * `smoothable` excludes absolute devices (tablets, and the pointer a VM
     * hands through): their "delta" is the distance to an absolute position, so
     * damping it makes the cursor trail the stylus instead of sitting under it. */
    if (smoothable && s->config.pointer_smoothing > 0 && !s->active_constraint)
        psmooth_apply(s, time_msec, device, &dx, &dy);
    else if (s->psmooth.pend_dev)
        psmooth_reset(s);

    wlr_cursor_move(s->cursor, device, dx, dy);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* A fullscreen game keeps the pointer on its own screen. This is NOT a
     * client constraint and deliberately does not go through constraints.c:
     * the games measured here never ask for one, so there is nothing to
     * honour and the compositor decides instead. Idempotent, and a no-op the
     * moment the game loses focus. */
    game_confine_cursor(s);

    /* An in-flight DnD icon rides the cursor (tree is empty otherwise). */
    wlr_scene_node_set_position(&s->drag_icon_tree->node,
                                (int)s->cursor->x, (int)s->cursor->y);

    /* …and if that drag is over the wallpaper, the desktop is the target. Told
     * here, BEFORE pointer_update_focus() below hands drag focus to whatever
     * surface the cursor moved onto: leaving the desktop and entering a window
     * are the same motion event, and our acceptance has to be withdrawn before
     * that client is offered the drag, not after. */
    if (s->seat->drag)
        deskdrop_hover(s, point_is_desktop(s, s->cursor->x, s->cursor->y));

    if (s->cursor_mode == SYNUI_CURSOR_MOVE)   { process_cursor_move(s);   return; }
    if (s->cursor_mode == SYNUI_CURSOR_RESIZE) { process_cursor_resize(s); return; }

    /* Dock drag-to-reposition floats the bar under the cursor. */
    if (s->panel_drag.active) {
        panel_drag_motion(s, s->cursor->x, s->cursor->y);
        return;
    }

    if (s->dock_drag.active) {
        dock_drag_motion(s, s->cursor->x, s->cursor->y);
        return;
    }
    /* Same for the crop rectangle being dragged out. */
    if (s->crop.dragging) {
        /* The crop rectangle, fed the same way the desktop-icon drag is: the
         * panel's _click starts it and the release below ends it. */
        crop_drag_motion(s, s->cursor->x, s->cursor->y);
        return;
    }

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
    if (panel_pointer_motion(s, s->cursor->x, s->cursor->y)) {
        panel_pointer_claim_cursor(s);
        return;
    }

    /* Let the auto-hide dock react to the cursor reaching its edge. */
    dock_pointer_motion(s);

    /* Light up the titlebar button under the pointer (repaints only on change),
     * and show the resize arrow for the edge it would drag. */
    deco_hover_update(s, s->cursor->x, s->cursor->y, time_msec);

    pointer_update_focus(s, time_msec);

    /* Last, and after pointer focus: keyboard focus following the pointer is
     * an addition to what the motion path already did, never a replacement
     * for part of it. A no-op unless focus_mode says otherwise. */
    focus_follow_pointer(s, time_msec);
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    process_pointer_motion(s, event->time_msec, &event->pointer->base,
                           event->delta_x, event->delta_y,
                           event->unaccel_dx, event->unaccel_dy, true);
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
                           lx - s->cursor->x, ly - s->cursor->y, false);
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
    /* Same as the key path: the click that dismisses the saver must not also
     * press whatever was underneath it. */
    if (saver_ate_event(s)) return;

    notify_activity(s);

    /* A click is the user saying where they are. If this one opens a panel, it
     * opens on the monitor the click landed on and stays there — see
     * server_ui_output_track(). Before any dispatch, so the panel this press is
     * about to open is not yet visible and cannot pin the previous answer. */
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        server_ui_output_track(s);

    /* Locked: a click may press one of the lock panel's own controls (the
     * media transport, the keyboard-layout chip) and otherwise does nothing
     * but wake the screen. Either way it goes no further — no window, dock or
     * panel underneath may be reached.
     *
     * The wake comes FIRST: a press on a dark screen is the user arriving, and
     * lock_handle_button declines it for exactly that reason, so the ordering
     * is what makes the first click brighten and the second one press. */
    if (s->nlock.active) {
        /* The press is offered to the panel BEFORE the wake, because
         * lock_handle_button declines a click on a faded-out screen — that
         * click is the user arriving, not a button they could see. Waking
         * first would set bright to 1.0 and make its own guard unreachable, so
         * the first click would press whatever happened to be under it. */
        if (state == WL_POINTER_BUTTON_STATE_PRESSED)
            lock_handle_button(s, s->cursor->x, s->cursor->y, button);
        lock_notify_activity(s);
        return;
    }

    /*
     * A client's drag-and-drop, let go over the wallpaper: the desktop takes it
     * (deskdrop.c copies the files into ~/Desktop).
     *
     * This has to come before anything that forwards the release, because
     * wlroots' drag grab reads a release with no CLIENT drag focus as a failed
     * drop and destroys the data source — which cancels the transfer we are in
     * the middle of asking for. So the data is claimed first and the drag is
     * then ended here rather than by the grab: wlr_seat_pointer_end_grab()
     * tears the drag down and leaves the source alive to finish writing.
     *
     * The release is still delivered afterwards, exactly as the untaken path
     * would have: wlroots decrements the seat's button count inside
     * notify_button, and until that happens pointer_update_focus() honours the
     * implicit grab and refuses to re-derive focus — the cursor would keep the
     * drag's image and enter nothing until the next motion event.
     *
     * point_is_desktop() is asked again here rather than trusted from the last
     * motion event: drag focus can also move because a window mapped or closed
     * under a stationary cursor, and no motion runs to withdraw the desktop's
     * acceptance on either.
     */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->seat->drag &&
        button == s->seat->pointer_state.grab_button &&
        point_is_desktop(s, s->cursor->x, s->cursor->y) &&
        deskdrop_take(s, s->cursor->x, s->cursor->y)) {
        wlr_seat_pointer_end_grab(s->seat);
        wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);
        pointer_update_focus(s, time_msec);
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
     * genuinely has nowhere else to go.
     *
     * crop.dragging is not a hypothetical the way the other two are: the
     * cropper IS a panel, so panel_pointer_active() is true for the whole of
     * every crop drag and this branch swallowed the mouse-up unconditionally.
     * crop_drag_end() never ran, dragging stayed set, and the rectangle went on
     * following the pointer after the button was released — and because
     * process_pointer_motion() returns early while it is set, no corner could
     * be grabbed afterwards either. */
    if (panel_pointer_active(s) &&
        state == WL_POINTER_BUTTON_STATE_RELEASED &&
        s->cursor_mode == SYNUI_CURSOR_PASSTHROUGH &&
        !s->dock_drag.active && !s->deskicon_drag.active &&
        !s->crop.dragging) {
        if (seat_button_is_down(s->seat, button))
            wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);
        return;
    }

    /* A dock drag keeps cursor_mode == PASSTHROUGH, so catch its release
     * before the generic grab-release below. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->panel_drag.active) {
        panel_drag_end(s);
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->dock_drag.active) {
        dock_drag_end(s, s->cursor->x, s->cursor->y);
        return;
    }

    /* The crop rectangle is the same story: PASSTHROUGH throughout, so its
     * release has to be caught here or the drag would never end. */
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->crop.dragging) {
        crop_drag_end(s, s->cursor->x, s->cursor->y);
        return;
    }

    /* And a desktop-icon drag, or the drop would never be committed. */
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
        syn_view_t *gv = s->grabbed_view;
        bool was_drag = !s->grab_armed;
        if (s->cursor_mode == SYNUI_CURSOR_MOVE && was_drag)
            snap_drag_end(s, gv);

        /*
         * Tell an X11 client where it ended up. An X client only learns its
         * root position from the ConfigureNotify we send it, and
         * process_cursor_move deliberately does not send one — it moves the
         * scene node directly, so a drag costs no X round-trip per motion
         * event. That leaves the X server holding the window's *pre-drag*
         * position for the whole move, and nothing ever corrected it
         * afterwards: view_resize() is the only place synui configures an X11
         * window, and a plain move never calls it.
         *
         * Steam's menus are override-redirect windows that Steam positions in
         * ROOT coordinates, computed from where it believes its own window is
         * — so after dragging Steam out of the maximized state its menus kept
         * opening over the position it was dragged from (velle, 2026-08-02,
         * screenshot synapse-20260802-154700.png). Re-assert the final
         * geometry once, here. It is idempotent, so a drop that snap_drag_end
         * already placed simply re-sends the same box.
         *
         * A RESIZE needs nothing: process_cursor_resize goes through
         * view_resize on every motion, which configures position and size
         * together.
         */
        if (gv && was_drag && s->cursor_mode == SYNUI_CURSOR_MOVE &&
            gv->is_xwayland && gv->mapped)
            view_resize(gv, gv->x, gv->y, gv->w, gv->h);

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

        /* Right-click the dock → its context menu: the app's rows when the
         * click landed on an icon, and the dock's own switches either way. The
         * bar body is the second half of that and not an afterthought — until it
         * was hit-tested here, the dock's settings had no pointer route at all
         * while the bar beside it put every one of its switches on a
         * right-click. See the comment over dockmenu_open(). */
        if (button == BTN_RIGHT) {
            syn_dock_entry_t *e =
                dock_entry_at(s, s->cursor->x, s->cursor->y);
            if (e) {
                dockmenu_open(s, e, s->cursor->x, s->cursor->y);
                return;
            }
            if (dock_bar_at(s, s->cursor->x, s->cursor->y, NULL)) {
                dockmenu_open(s, NULL, s->cursor->x, s->cursor->y);
                return;
            }
        }

        if (button == BTN_LEFT) {
            /* The show-all-apps button, before the icons and well before the
             * bar: it is drawn ON the body, so a press that reached
             * dock_bar_at() below would start dragging the whole dock to
             * another edge instead. Every installed app, which is the one thing
             * a row of pinned icons cannot offer. */
            if (dock_apps_at(s, s->cursor->x, s->cursor->y)) {
                /* ⚠ ARMS A DRAG; it does NOT open the overlay here any more.
                 * The cell can be moved along the run like the clock, and a
                 * button that acted on press would fire on the way into every
                 * such gesture. dock_drag_end() runs the click when the press
                 * never travelled — and it is appgrid_toggle() there, NOT
                 * synui_start_menu_open(). This button IS the application
                 * overlay: that is what the grid-of-dots draws and the only
                 * thing it has ever meant. Desktop ▸ Start menu chooses what
                 * the SUPER TAP opens, which is a keystroke with no picture on
                 * it and therefore nothing to disagree with. Routing a labelled
                 * button through that row makes the button's own meaning depend
                 * on a setting elsewhere: velle's says Rofi, so pressing the
                 * app-grid icon opened Rofi. The start button on the bar does
                 * not change what it opens either. */
                dock_apps_drag_begin(s, s->cursor->x, s->cursor->y);
                return;
            }
            /* …the power button, which on release opens its own menu rather
             * than doing anything — see dockmenu_open_power() for why a menu
             * and not five buttons. Same ordering rule, same reason, and the
             * same press-arms-a-drag contract as the apps button above. A
             * RIGHT click here is deliberately not special-cased: it falls
             * through to dock_bar_at() below and opens the dock's settings
             * menu, exactly as a right click on the apps button does. */
            if (dock_power_at(s, s->cursor->x, s->cursor->y)) {
                dock_power_drag_begin(s, s->cursor->x, s->cursor->y);
                return;
            }
            /* …and the clock, the third cell and the one with no click to owe.
             * Same ordering rule, same reason. */
            if (dock_clock_at(s, s->cursor->x, s->cursor->y)) {
                dock_clock_drag_begin(s, s->cursor->x, s->cursor->y);
                return;
            }
            syn_dock_entry_t *dock_hit =
                dock_entry_at(s, s->cursor->x, s->cursor->y);
            if (dock_hit) {
                /* NOT dock_entry_click() any more. A press on an icon arms the
                 * drag-to-rearrange; the release runs the click if the pointer
                 * never travelled far enough to be a drag. Launching on press
                 * and reordering on release would mean every rearrange also
                 * raised or launched the app it moved. */
                dock_icon_drag_begin(s, dock_hit, s->cursor->x, s->cursor->y);
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
                case DECO_BORDER: {
                    /* Double-click a border to fill the usable box along that
                     * border's axis — top or bottom grows it vertically, left
                     * or right horizontally. The same shape as the titlebar
                     * double-click directly below, and the same 400 ms.
                     *
                     * ⚠ THE EDGE IS PART OF THE IDENTITY. Two quick clicks on
                     * two different edges are two gestures that happened to be
                     * close together, not one double-click, and treating them
                     * as one expands an axis nobody pointed at. */
                    bool dbl = (s->bd_last_click_view == dv) &&
                               (s->bd_last_click_edges == edges) &&
                               (time_msec - s->bd_last_click_ms < 400);
                    s->bd_last_click_view  = dbl ? NULL : dv;
                    s->bd_last_click_edges = edges;
                    s->bd_last_click_ms    = time_msec;
                    if (dbl)
                        view_apply_edge_expand(s, dv, edges);
                    else
                        /* ARMED, like the titlebar. A press on a border must
                         * not commit to anything until the pointer has
                         * travelled: committing at press time un-maximized,
                         * un-tiled and hand-placed the window on the FIRST
                         * click of every double-click, so the second one could
                         * never find an axis still expanded to collapse. It
                         * also stopped a click that merely brushed the border
                         * from resizing the window by a pixel. */
                        begin_interactive_armed(dv, SYNUI_CURSOR_RESIZE,
                                                edges, true);
                    break;
                }
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

            /* The desktop takes the KEYBOARD too.
             *
             * Without this the click selected a desktop icon while the last
             * window kept keyboard focus, so the next keystroke went somewhere
             * the user was not looking. velle hit the worst version of it:
             * clicked a file on the desktop, pressed Delete, and the file
             * browser behind deleted ITS selection instead. Whatever the
             * desktop is about to do with a key, the window must not do it. */
            if (button == BTN_LEFT || button == BTN_RIGHT)
                focus_view(s, NULL, NULL);

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
            /* An offset and not a scale, which is right for every surface
             * synui does not resize the buffer of — i.e. all of them but a
             * sub-native fullscreen X11 game (view_fullscreen_rescale). Inside
             * one of those the surface counts in different units from the
             * layout, so this replays a drag slightly compressed; the same
             * space mismatch constraints.c documents at length. */
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

    /* Super+click (move/resize a window) is the tap key used as a modifier, so
     * it must not also open the start menu when that key is finally released. */
    s->tap_armed = 0;

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

    /*
     * A WINDOWED panel is not modal, so it never reaches the branch above — but
     * the wheel over its OWN rows is still its own. Without this the control
     * panel, the calculator's tape and the task manager's process list simply
     * did not scroll in window mode: the event went straight past them to
     * whatever client happened to be under the pointer, which usually scrolled
     * instead. The panel looked frozen and the window behind it moved.
     *
     * Offered, not taken: each _scroll() declines a point that is off it — the
     * same test its _motion() already makes — so falling through to the client
     * here cannot resurrect the desktop-wide wheel swallowing that the modality
     * filter (panel_mem_is_modal) exists to stop. The wheel belongs to whatever
     * is under the pointer, and over a windowed panel that IS the panel.
     */
    if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        panel_pointer_scroll(s, s->cursor->x, s->cursor->y, event->delta))
        return;

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

/*
 * A virtual POINTER (virtual-pointer-v1) — the mirror of
 * server_new_virtual_keyboard(), and it takes the same shortcut:
 * wlr_virtual_pointer_v1 wraps a real struct wlr_pointer, so handing it to
 * wlr_cursor makes it a mouse in every way that matters. Motion goes through
 * the same cursor, lands on the surface the same focus rules pick, and draws
 * the same cursor image on screen.
 *
 * ⚠ Deliberately NOT reached through server_new_input() below. That is the
 * BACKEND's signal and a virtual device never passes through it — the identical
 * arrangement virtual keyboards have here, and the reason both of these exist
 * rather than one switch statement.
 *
 * Added for syn-arcade's big screen mode, where a gamepad stick has to move a
 * pointer through somebody's web browser: a browser takes pointer events, and
 * no amount of words on a pipe is one. The client may suggest an output and
 * that is honoured, because big screen mode is on the television and a cursor
 * that came up on the desk monitor is a cursor nobody can see moving.
 */
static void server_new_virtual_pointer(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *ev = data;
    struct wlr_input_device *dev = &ev->new_pointer->pointer.base;

    /* Tracked like any other pointer, so a config reload revisits it and the
     * device's own destroy tears the bookkeeping down. Applying the libinput
     * config is a no-op for a device libinput never made — it checks — and is
     * called anyway so this path cannot drift from the real one. */
    input_apply_libinput_config(s, dev);
    input_dev_track(s, dev);
    wlr_cursor_attach_input_device(s->cursor, dev);

    if (ev->suggested_output)
        wlr_cursor_map_input_to_output(s->cursor, dev, ev->suggested_output);

    seat_update_capabilities(s);
    wlr_log(WLR_INFO, "synui: input: virtual pointer attached");
}

/* Same shutdown rule as the keyboard manager: wlroots asserts nobody is still
 * subscribed when the display tears the manager down. */
static void server_vptr_mgr_destroy(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, vptr_mgr_destroy);
    wl_list_remove(&s->new_virtual_pointer.link);
    wl_list_remove(&s->vptr_mgr_destroy.link);
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
    /* However it ended — dropped on a client, taken by the desktop, or
     * cancelled — the acceptance the desktop may have given this drag dies with
     * it. Anything already in flight is a transfer now, not a drag. */
    deskdrop_reset(s);
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

    /* Smoothing is synui's own filter, not a device option, so the loop above
     * cannot reach it. Drop whatever is pending: the reload may have turned the
     * setting off or changed its strength, and a remainder measured against the
     * old time constant is not a movement the new one should finish. */
    psmooth_reset(s);
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

    /* virtual-pointer-v1 (syn-arcade's controller-as-mouse — see
     * server_new_virtual_pointer). Privileged: it is in privileged_globals[]
     * so a sandboxed client cannot bind it. */
    s->virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(s->display);
    s->new_virtual_pointer.notify = server_new_virtual_pointer;
    wl_signal_add(&s->virtual_pointer_mgr->events.new_virtual_pointer,
                 &s->new_virtual_pointer);
    s->vptr_mgr_destroy.notify = server_vptr_mgr_destroy;
    wl_signal_add(&s->virtual_pointer_mgr->events.destroy,
                 &s->vptr_mgr_destroy);
}
