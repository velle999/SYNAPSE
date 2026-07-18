/*
 * lock.c — the native lock screen.
 *
 * synui is the ext-session-lock *compositor* (session.c); this is the lock
 * *itself*, drawn by the compositor instead of by a spawned swaylock. The point
 * is the look: a big clock that brightens the instant you touch a key or the
 * mouse and fades back to black after a few idle seconds — a locked screen you
 * can read at a glance, not swaylock's featureless black rectangle (which, with
 * pam_faillock behind it, is the "black screen that won't take my password"
 * that stranded the session more than once).
 *
 * It reuses `s->locked` — the same flag the ext-session-lock path sets — so
 * every input gate already written for the session lock (keyboard forwarding,
 * pointer focus, the VT-switch escape hatch that runs *before* the lock check)
 * holds here unchanged. What differs is where a key goes while locked: to
 * lock_handle_key() below, not to a client surface.
 *
 * AUTH runs in a child (synui-lock-auth), never in the compositor: pam_unix's
 * fail delay is ~2s and pam_faillock can block for minutes, and the wl_event
 * loop must not stall for either. The password goes to the child down a pipe
 * (never argv — that is world-readable in ps), the verdict comes back up
 * another pipe read off the event loop, and the child is reaped by the global
 * SIGCHLD handler like every other — so SIGCHLD is blocked across the fork, as
 * everywhere else in this tree (see ai_interface.c and
 * reference-inherited-signal-dispositions).
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Panel drawn on each output, centred. Fixed-size (not the whole output) so a
 * redraw allocates a ~kB buffer, not a full framebuffer, 30 times a second
 * while it fades. */
#define LOCK_PANEL_W    720
#define LOCK_PANEL_H    360

/* Hold bright this long after the last input, then ease to black. "A few
 * seconds", as asked. */
#define LOCK_HOLD_MS    4000
#define LOCK_FADE_STEP  0.05     /* per 33 ms tick ⇒ ~0.7 s to fade out */

static uint32_t lock_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Drawing ─────────────────────────────────────────────── */

/* One panel: the clock, the date, and the password dots — everything scaled by
 * `bright`, so at bright 0 the buffer is empty (transparent over the black
 * backstop) and the screen is dark. */
static void lock_draw_panel(syn_server_t *s, cairo_t *cr)
{
    double a = s->nlock.bright;
    if (a <= 0.004) return;      /* faded out: leave it transparent */

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    char hhmm[16], ampm[8], date[64];
    strftime(hhmm, sizeof(hhmm), "%-I:%M", &tm);   /* 12h, matches the bar */
    strftime(ampm, sizeof(ampm), "%p", &tm);
    strftime(date, sizeof(date), "%A, %B %-d", &tm);

    double cx = LOCK_PANEL_W / 2.0;

    /* Clock — big, centred. */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 120);
    cairo_text_extents_t te;
    cairo_text_extents(cr, hhmm, &te);
    /* A soft cyan glow, then the glyphs — the SYNAPSE accent. */
    cairo_set_source_rgba(cr, 0.02, 0.85, 0.75, 0.18 * a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing + 2, 150 + 2);
    cairo_show_text(cr, hhmm);
    cairo_set_source_rgba(cr, 0.85, 0.98, 1.0, a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 150);
    cairo_show_text(cr, hhmm);

    /* AM/PM, small, trailing the clock. */
    cairo_set_font_size(cr, 30);
    cairo_set_source_rgba(cr, 0.45, 0.9, 0.85, a);
    cairo_move_to(cr, cx + te.width / 2 + 12, 150);
    cairo_show_text(cr, ampm);

    /* Date. */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    cairo_text_extents(cr, date, &te);
    cairo_set_source_rgba(cr, 0.62, 0.72, 0.80, a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 205);
    cairo_show_text(cr, date);

    /* Password row: dots while typing, or the error after a rejection. The row
     * is only worth drawing once there is something to say. */
    if (s->nlock.failed) {
        const char *msg = "Wrong password";
        cairo_set_font_size(cr, 18);
        cairo_text_extents(cr, msg, &te);
        cairo_set_source_rgba(cr, 1.0, 0.36, 0.42, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 275);
        cairo_show_text(cr, msg);
    } else if (s->nlock.busy) {
        const char *msg = "Checking\xe2\x80\xa6";
        cairo_set_font_size(cr, 18);
        cairo_text_extents(cr, msg, &te);
        cairo_set_source_rgba(cr, 0.45, 0.9, 0.85, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 275);
        cairo_show_text(cr, msg);
    } else if (s->nlock.pw_len > 0) {
        /* A dot per character, capped so a held key cannot draw off-panel. */
        int dots = s->nlock.pw_len;
        if (dots > 24) dots = 24;
        double r = 5, gap = 18;
        double total = (dots - 1) * gap;
        double x = cx - total / 2;
        cairo_set_source_rgba(cr, 0.75, 0.85, 0.92, a);
        for (int i = 0; i < dots; i++) {
            cairo_arc(cr, x + i * gap, 268, r, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }
    }
}

/* Render every pane. Panels are created lazily here so synui_lock() need only
 * record the outputs. Non-static so the greeter (greeter.c) can repaint the
 * shared panel after a rejected login. */
void lock_render(syn_server_t *s)
{
    if (!s->nlock.active || !s->nlock.tree) return;

    for (int i = 0; i < s->nlock.npane; i++) {
        struct wlr_output *o = s->nlock.pane[i].output;
        if (!o) continue;

        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, o, &box);
        if (box.width <= 0 || box.height <= 0) continue;   /* output went away */

        cairo_t *cr;
        struct wlr_buffer *buf = create_cairo_buf(LOCK_PANEL_W, LOCK_PANEL_H, &cr);
        if (!buf) continue;
        cairo_begin(cr);
        lock_draw_panel(s, cr);
        cairo_destroy(cr);

        set_scene_buffer(&s->nlock.pane[i].buf, s->nlock.tree, buf);

        int px = box.x + (box.width  - LOCK_PANEL_W) / 2;
        int py = box.y + (box.height - LOCK_PANEL_H) / 2;
        wlr_scene_node_set_position(&s->nlock.pane[i].buf->node, px, py);
    }
}

/* ── Fade / clock timers ─────────────────────────────────── */

static int lock_fade_cb(void *data)
{
    syn_server_t *s = data;
    if (!s->nlock.active) return 0;

    uint32_t idle = lock_now_ms() - s->nlock.last_input_ms;
    if (idle < LOCK_HOLD_MS) {
        /* Still within the hold: nothing to do but check back when it ends. */
        if (s->nlock.t_fade)
            wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS - idle);
        return 0;
    }

    s->nlock.bright -= LOCK_FADE_STEP;
    if (s->nlock.bright <= 0.0) {
        s->nlock.bright = 0.0;
        lock_render(s);          /* one last frame: fully dark */
        return 0;                /* stop — re-armed by the next input */
    }
    lock_render(s);
    if (s->nlock.t_fade)
        wl_event_source_timer_update(s->nlock.t_fade, 33);
    return 0;
}

static int lock_clock_cb(void *data)
{
    syn_server_t *s = data;
    if (!s->nlock.active) return 0;
    if (s->nlock.bright > 0.02)      /* nothing visible ⇒ nothing to update */
        lock_render(s);
    if (s->nlock.t_clock)
        wl_event_source_timer_update(s->nlock.t_clock, 1000);
    return 0;
}

/* Snap to full brightness and reschedule the fade. Called on every key and on
 * pointer motion while locked. */
void lock_notify_activity(syn_server_t *s)
{
    if (!s->nlock.active) return;
    s->nlock.last_input_ms = lock_now_ms();
    if (s->nlock.bright < 1.0) {
        s->nlock.bright = 1.0;
        lock_render(s);
    }
    if (s->nlock.t_fade)
        wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS);
}

/* ── Authentication ──────────────────────────────────────── */

static void lock_auth_cleanup(syn_server_t *s)
{
    if (s->nlock.auth_src) {
        wl_event_source_remove(s->nlock.auth_src);
        s->nlock.auth_src = NULL;
    }
    if (s->nlock.auth_fd >= 0) {
        close(s->nlock.auth_fd);
        s->nlock.auth_fd = -1;
    }
    s->nlock.busy = 0;
}

static int lock_auth_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n < 0 && errno == EINTR) return 0;   /* interrupted; try again when ready */

    int ok = (n == 1 && c == '1');           /* anything else — a '0', or EOF — fails */
    lock_auth_cleanup(s);

    if (ok) {
        wlr_log(WLR_INFO, "synui: lock: authenticated");
        synui_unlock(s);
    } else {
        wlr_log(WLR_INFO, "synui: lock: authentication failed");
        s->nlock.failed = 1;
        lock_notify_activity(s);             /* wake the screen to show the error */
        lock_render(s);
    }
    return 0;
}

static void lock_auth_start(syn_server_t *s)
{
    if (s->nlock.busy || s->nlock.pw_len == 0) return;

    int rp[2], pp[2];                        /* result pipe, password pipe */
    if (pipe2(rp, O_CLOEXEC) < 0) return;
    if (pipe2(pp, O_CLOEXEC) < 0) { close(rp[0]); close(rp[1]); return; }

    /* Block SIGCHLD across the fork: the global reap_children() must not reap
     * this child before we have wired its result fd up. */
    sigset_t chld, prev;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &prev);

    pid_t pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &prev, NULL);
        close(rp[0]); close(rp[1]); close(pp[0]); close(pp[1]);
        return;
    }
    if (pid == 0) {
        /* Child: password on stdin, verdict on stdout. The O_CLOEXEC ends we
         * keep are cleared by dup2 (which drops the flag on the new fd). */
        dup2(pp[0], STDIN_FILENO);
        dup2(rp[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); if (devnull > 2) close(devnull); }
        synui_child_reset_signals();
        execlp("synui-lock-auth", "synui-lock-auth", (char *)NULL);
        _exit(127);
    }

    /* Parent. */
    close(pp[0]);
    close(rp[1]);

    /* Hand the password over, then wipe our copy — it has no reason to sit in
     * the compositor's heap a moment longer than the write. */
    ssize_t off = 0;
    while (off < s->nlock.pw_len) {
        ssize_t w = write(pp[1], s->nlock.pw + off, s->nlock.pw_len - off);
        if (w <= 0) { if (errno == EINTR) continue; break; }
        off += w;
    }
    (void)!write(pp[1], "\n", 1);
    close(pp[1]);
    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    sigprocmask(SIG_SETMASK, &prev, NULL);

    s->nlock.auth_pid = pid;
    s->nlock.auth_fd  = rp[0];
    s->nlock.busy     = 1;
    s->nlock.failed   = 0;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->nlock.auth_src = wl_event_loop_add_fd(loop, rp[0], WL_EVENT_READABLE,
                                             lock_auth_readable, s);
    lock_render(s);                          /* show "Checking…" */
}

/* ── Keyboard ────────────────────────────────────────────── */

/* Append one Unicode code point to the password as UTF-8. */
static void lock_pw_append(syn_server_t *s, uint32_t cp)
{
    char u[4];
    int n;
    if      (cp < 0x80)    { u[0] = cp; n = 1; }
    else if (cp < 0x800)   { u[0] = 0xC0 | (cp >> 6); u[1] = 0x80 | (cp & 0x3F); n = 2; }
    else if (cp < 0x10000) { u[0] = 0xE0 | (cp >> 12); u[1] = 0x80 | ((cp >> 6) & 0x3F);
                             u[2] = 0x80 | (cp & 0x3F); n = 3; }
    else                   { u[0] = 0xF0 | (cp >> 18); u[1] = 0x80 | ((cp >> 12) & 0x3F);
                             u[2] = 0x80 | ((cp >> 6) & 0x3F); u[3] = 0x80 | (cp & 0x3F); n = 4; }

    if (s->nlock.pw_len + n >= (int)sizeof(s->nlock.pw)) return;  /* full: drop it */
    memcpy(s->nlock.pw + s->nlock.pw_len, u, n);
    s->nlock.pw_len += n;
    s->nlock.pw[s->nlock.pw_len] = 0;
}

int lock_handle_key(syn_server_t *s, xkb_keysym_t sym, uint32_t codepoint)
{
    if (!s->nlock.active) return 0;

    lock_notify_activity(s);        /* any key brightens the screen */
    if (s->nlock.busy) return 1;    /* a check is in flight: swallow, do nothing */

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        /* In the greeter there is no session to unlock — hand the password to
         * greetd to start one instead. Same panel, different verb. */
        if (s->greeter)
            greeter_submit(s);
        else
            lock_auth_start(s);
        return 1;
    case XKB_KEY_BackSpace:
        if (s->nlock.pw_len > 0) {
            /* Step back over a whole UTF-8 code point, not one byte. */
            do { s->nlock.pw_len--; }
            while (s->nlock.pw_len > 0 &&
                   (s->nlock.pw[s->nlock.pw_len] & 0xC0) == 0x80);
            s->nlock.pw[s->nlock.pw_len] = 0;
        }
        s->nlock.failed = 0;
        lock_render(s);
        return 1;
    case XKB_KEY_Escape:
        s->nlock.pw_len = 0;
        s->nlock.pw[0] = 0;
        s->nlock.failed = 0;
        lock_render(s);
        return 1;
    default:
        /* A printable character types into the password; everything else is
         * swallowed, because while locked no key may reach anything else. */
        if (codepoint >= 0x20 && codepoint != 0x7f) {
            lock_pw_append(s, codepoint);
            s->nlock.failed = 0;
            lock_render(s);
        }
        return 1;
    }
}

/* ── Lock / unlock ───────────────────────────────────────── */

void synui_lock(syn_server_t *s)
{
    if (s->locked) return;          /* idempotent: already locked (native or client) */

    s->locked        = 1;
    s->nlock.active  = 1;
    s->nlock.busy    = 0;
    s->nlock.failed  = 0;
    s->nlock.bright  = 1.0;
    s->nlock.pw_len  = 0;
    s->nlock.pw[0]   = 0;
    s->nlock.auth_fd = -1;
    s->nlock.last_input_ms = lock_now_ms();

    /* Take keyboard focus away from every window first. */
    wlr_seat_keyboard_notify_clear_focus(s->seat);

    /* Overlay above everything: a black backstop over the whole layout (so an
     * output with no pane, or one hotplugged mid-lock, shows black, never the
     * desktop), then a clock panel per output on top of it. */
    s->nlock.tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_raise_to_top(&s->nlock.tree->node);
    float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    struct wlr_scene_rect *bg =
        wlr_scene_rect_create(s->nlock.tree, 32768, 32768, black);
    wlr_scene_node_set_position(&bg->node, -16384, -16384);

    s->nlock.npane = 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (s->nlock.npane >= (int)(sizeof(s->nlock.pane) / sizeof(s->nlock.pane[0])))
            break;
        s->nlock.pane[s->nlock.npane].output = o->wlr_output;
        s->nlock.pane[s->nlock.npane].buf    = NULL;
        s->nlock.npane++;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->nlock.t_clock = wl_event_loop_add_timer(loop, lock_clock_cb, s);
    s->nlock.t_fade  = wl_event_loop_add_timer(loop, lock_fade_cb, s);
    wl_event_source_timer_update(s->nlock.t_clock, 1000);
    wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS);

    lock_render(s);
    wlr_log(WLR_INFO, "synui: session locked (native)");
}

void synui_unlock(syn_server_t *s)
{
    if (!s->nlock.active) return;

    s->nlock.active = 0;
    s->locked = 0;

    lock_auth_cleanup(s);
    if (s->nlock.t_clock) { wl_event_source_remove(s->nlock.t_clock); s->nlock.t_clock = NULL; }
    if (s->nlock.t_fade)  { wl_event_source_remove(s->nlock.t_fade);  s->nlock.t_fade  = NULL; }

    if (s->nlock.tree) {
        wlr_scene_node_destroy(&s->nlock.tree->node);   /* takes the panes with it */
        s->nlock.tree = NULL;
    }
    for (int i = 0; i < s->nlock.npane; i++) s->nlock.pane[i].buf = NULL;
    s->nlock.npane = 0;

    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    /* Hand focus back to a window, as session.c's unlock does. */
    if (s->focused_view && s->focused_view->mapped)
        focus_view(s, s->focused_view, view_surface(s->focused_view));
    else
        wlr_seat_keyboard_notify_clear_focus(s->seat);

    /* Make sure the outputs are lit and the idle timers rearmed — the same path
     * a keypress takes to undo a DPMS blank. */
    power_notify_activity(s);
    wlr_log(WLR_INFO, "synui: session unlocked (native)");
}
