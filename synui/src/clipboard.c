/*
 * clipboard.c — clipboard history (Super+V), natively.
 *
 * synui_main.c already wires data-control (the protocol cliphist uses) and said
 * so in a comment — but SynapseOS shipped no clipboard manager, so copying
 * something and then copying something else lost the first thing forever, like
 * every bare Wayland session.
 *
 * Native because the compositor is already the clipboard: it owns the seat, so
 * it sees every selection without a protocol, a client, or a second process
 * polling wl-paste. No history file either — a clipboard silently persisting
 * every password you copy to disk is a liability, so this lives in memory and
 * dies with the session.
 *
 * TWO THINGS HERE MUST NOT BLOCK THE COMPOSITOR, and both are pipes to clients
 * that may never read or write:
 *
 *   - Reading a new selection. The owning client writes into a pipe at its own
 *     pace; a blocking read would freeze every window until it got around to it.
 *     The read fd goes in the wl_event_loop.
 *   - Serving our own history back. The receiving client reads at its pace, and
 *     a pipe holds 64K — a blocking write of a larger clipboard would deadlock
 *     the compositor against a client that is waiting on the compositor. The
 *     write fd goes in the loop too, and partial writes are resumed.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* The only type worth keeping. A clipboard manager that tries to hold every
 * flavour a client offers (image/png, text/html, x-special/*) is a memory
 * problem and a rendering problem; text is what people actually want back. */
#define CLIP_MIME "text/plain;charset=utf-8"

/* An in-flight read of a client's selection. */
struct clip_read {
    syn_server_t           *server;
    int                     fd;
    struct wl_event_source *src;
    char                   *buf;
    size_t                  len;
};

/* An in-flight write of our history back to a client. */
struct clip_write {
    int                     fd;
    struct wl_event_source *src;
    char                   *buf;
    size_t                  len, off;
};

/* Our own data source: what the seat offers after you pick from the history. */
struct clip_source {
    struct wlr_data_source base;
    char                  *text;
};

static struct {
    /* Set while we are the selection owner, so the set_selection handler can
     * tell our own source from a client's and not re-record what we just
     * offered — which would duplicate the entry on every paste. */
    struct clip_source *ours;
} clip;

/* ── History ─────────────────────────────────────────────── */

static void clip_history_add(syn_server_t *s, const char *text)
{
    syn_clipboard_t *c = &s->clipboard;
    if (!text || !*text) return;

    /* Re-copying something already at the top is not a new entry. */
    if (c->count && strcmp(c->items[0].text, text) == 0) return;

    /* If it is further down, lift it rather than duplicate it. */
    for (int i = 1; i < c->count; i++) {
        if (strcmp(c->items[i].text, text) != 0) continue;
        syn_clip_item_t tmp = c->items[i];
        memmove(&c->items[1], &c->items[0], (size_t)i * sizeof(c->items[0]));
        c->items[0] = tmp;
        return;
    }

    if (c->count == CLIP_HISTORY_MAX) {
        free(c->items[c->count - 1].text);
        c->count--;
    }
    memmove(&c->items[1], &c->items[0], (size_t)c->count * sizeof(c->items[0]));

    c->items[0].text = strdup(text);
    if (!c->items[0].text) { memmove(&c->items[0], &c->items[1],
                                     (size_t)c->count * sizeof(c->items[0]));
                             return; }
    c->count++;
}

void clipboard_clear(syn_server_t *s)
{
    for (int i = 0; i < s->clipboard.count; i++) free(s->clipboard.items[i].text);
    s->clipboard.count = 0;
    s->clipboard.selected = 0;
    if (s->clipboard.visible) synui_render_clipboard(s);
}

/* ── Reading a client's selection ────────────────────────── */

static void clip_read_finish(struct clip_read *r)
{
    if (r->src) wl_event_source_remove(r->src);
    if (r->fd >= 0) close(r->fd);
    free(r->buf);
    free(r);
}

static int clip_read_cb(int fd, uint32_t mask, void *data)
{
    struct clip_read *r = data;

    if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
        /* Hangup with data already read is the normal end: the client wrote and
         * closed. Only treat it as the end, not as a failure. */
        if (r->len) {
            r->buf[r->len] = '\0';
            clip_history_add(r->server, r->buf);
            if (r->server->clipboard.visible) synui_render_clipboard(r->server);
        }
        clip_read_finish(r);
        return 0;
    }

    for (;;) {
        /* Cap it. A client is free to offer a 900MB "text/plain" selection, and
         * a clipboard manager is not a reason to hold that in the compositor. */
        if (r->len >= CLIP_TEXT_MAX) {
            wlr_log(WLR_DEBUG, "synui: clipboard: selection over %d bytes — dropped",
                    CLIP_TEXT_MAX);
            clip_read_finish(r);
            return 0;
        }

        size_t space = CLIP_TEXT_MAX - r->len;
        char *nb = realloc(r->buf, r->len + (space > 4096 ? 4096 : space) + 1);
        if (!nb) { clip_read_finish(r); return 0; }
        r->buf = nb;

        ssize_t n = read(fd, r->buf + r->len, space > 4096 ? 4096 : space);
        if (n > 0) { r->len += (size_t)n; continue; }
        if (n == 0) {                       /* EOF: the client is done */
            r->buf[r->len] = '\0';
            clip_history_add(r->server, r->buf);
            if (r->server->clipboard.visible) synui_render_clipboard(r->server);
            clip_read_finish(r);
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  /* more later */
        if (errno == EINTR) continue;
        clip_read_finish(r);                /* a real error */
        return 0;
    }
}

/* Ask the current selection owner for its text, without waiting for it. */
static void clip_capture(syn_server_t *s, struct wlr_data_source *source)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return;

    struct clip_read *r = calloc(1, sizeof(*r));
    if (!r) { close(fds[0]); close(fds[1]); return; }
    r->server = s;
    r->fd = fds[0];

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    r->src = wl_event_loop_add_fd(loop, fds[0], WL_EVENT_READABLE, clip_read_cb, r);
    if (!r->src) { close(fds[0]); close(fds[1]); free(r); return; }

    /* wlr_data_source_send closes the write end for us. If it did not, the read
     * end would never see EOF and the entry would never land. */
    wlr_data_source_send(source, CLIP_MIME, fds[1]);
}

static bool source_has_text(struct wlr_data_source *source)
{
    char **mime;
    wl_array_for_each(mime, &source->mime_types)
        if (strcmp(*mime, CLIP_MIME) == 0) return true;
    return false;
}

static void handle_set_selection(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, clipboard_set_selection);
    (void)data;

    struct wlr_data_source *source = s->seat->selection_source;
    if (!source) return;

    /* Our own source, offered because someone picked from the history — do not
     * read it back in, or every paste duplicates the entry it came from. */
    if (clip.ours && source == &clip.ours->base) return;

    if (!source_has_text(source)) return;   /* an image, a file drag: not ours */
    clip_capture(s, source);
}

/* ── Serving our history back ────────────────────────────── */

static void clip_write_finish(struct clip_write *w)
{
    if (w->src) wl_event_source_remove(w->src);
    if (w->fd >= 0) close(w->fd);
    free(w->buf);
    free(w);
}

static int clip_write_cb(int fd, uint32_t mask, void *data)
{
    struct clip_write *w = data;

    /* The receiver gave up (closed its end). Not an error — a client is allowed
     * to ask and then lose interest, and we must not die on the EPIPE. */
    if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) { clip_write_finish(w); return 0; }

    while (w->off < w->len) {
        ssize_t n = write(fd, w->buf + w->off, w->len - w->off);
        if (n > 0) { w->off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n < 0 && errno == EINTR) continue;
        break;                              /* EPIPE or a real error */
    }
    clip_write_finish(w);                   /* done, or gave up */
    return 0;
}

static void clip_source_send(struct wlr_data_source *source, const char *mime,
                             int32_t fd)
{
    struct clip_source *cs = (struct clip_source *)source;

    if (strcmp(mime, CLIP_MIME) != 0 || !cs->text) { close(fd); return; }

    /* NON-BLOCKING, and this is the whole point: a pipe holds 64K. Writing a
     * larger clipboard straight down it blocks until the client reads — and the
     * client is very often blocked waiting on the compositor, which is us. That
     * is a deadlock of the entire desktop over a big copy-paste. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct clip_write *w = calloc(1, sizeof(*w));
    if (!w) { close(fd); return; }
    w->fd = fd;
    w->len = strlen(cs->text);
    w->buf = malloc(w->len ? w->len : 1);
    if (!w->buf) { close(fd); free(w); return; }
    memcpy(w->buf, cs->text, w->len);

    /* The loop is reachable through the server; the source outlives neither. */
    extern struct wl_event_loop *synui_clip_loop(void);
    struct wl_event_loop *loop = synui_clip_loop();
    if (!loop) { clip_write_finish(w); return; }

    w->src = wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, clip_write_cb, w);
    if (!w->src) { clip_write_finish(w); return; }

    /* Try immediately: most clipboards are small and finish here without ever
     * waking the loop. */
    clip_write_cb(fd, 0, w);
}

static void clip_source_accept(struct wlr_data_source *source, uint32_t serial,
                               const char *mime)
{
    (void)source; (void)serial; (void)mime;
}

static void clip_source_destroy(struct wlr_data_source *source)
{
    struct clip_source *cs = (struct clip_source *)source;
    if (clip.ours == cs) clip.ours = NULL;
    free(cs->text);
    free(cs);
}

static const struct wlr_data_source_impl clip_source_impl = {
    .send = clip_source_send,
    .accept = clip_source_accept,
    .destroy = clip_source_destroy,
};

/* Put an entry back on the clipboard, so the next Ctrl+V pastes it. */
static void clip_offer(syn_server_t *s, const char *text)
{
    struct clip_source *cs = calloc(1, sizeof(*cs));
    if (!cs) return;
    cs->text = strdup(text);
    if (!cs->text) { free(cs); return; }

    wlr_data_source_init(&cs->base, &clip_source_impl);

    char **p = wl_array_add(&cs->base.mime_types, sizeof(char *));
    if (!p) { wlr_data_source_destroy(&cs->base); return; }
    *p = strdup(CLIP_MIME);
    if (!*p) { wlr_data_source_destroy(&cs->base); return; }

    clip.ours = cs;
    /* The serial matters: the seat rejects a selection set with a stale one. The
     * keypress that got us here is the most recent thing the seat saw. */
    wlr_seat_set_selection(s->seat, &cs->base,
                           wl_display_next_serial(s->display));
}

/* ── Panel ───────────────────────────────────────────────── */

void clipboard_show(syn_server_t *s)
{
    s->clipboard.visible = 1;
    s->clipboard.selected = 0;
    s->clipboard.scroll = 0;
    synui_render_clipboard(s);
}

void clipboard_hide(syn_server_t *s)
{
    s->clipboard.visible = 0;
    synui_render_clipboard(s);
    ctlpanel_child_closed(s, "clipboard");
}

void clipboard_toggle(syn_server_t *s)
{
    if (s->clipboard.visible) clipboard_hide(s);
    else                      clipboard_show(s);
}

static void clip_move(syn_clipboard_t *c, int dir)
{
    int n = c->selected + dir;
    if (n < 0 || n >= c->count) return;
    c->selected = n;
    if (c->selected < c->scroll) c->scroll = c->selected;
    if (c->selected >= c->scroll + CLIP_ROWS) c->scroll = c->selected - CLIP_ROWS + 1;
}

int clipboard_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_clipboard_t *c = &s->clipboard;
    if (!c->visible) return 0;

    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        clipboard_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        clip_move(c, -1); synui_render_clipboard(s); return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        clip_move(c, +1); synui_render_clipboard(s); return 1;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (c->selected >= 0 && c->selected < c->count) {
            /* Copy the text out before hiding: offering it re-enters
             * handle_set_selection, and the history can reorder underneath. */
            char *text = strdup(c->items[c->selected].text);
            clipboard_hide(s);
            if (text) { clip_offer(s, text); free(text); }
        }
        return 1;
    case XKB_KEY_Delete:
        clipboard_clear(s);
        return 1;
    default:
        return 1;   /* modal */
    }
}

/* ── Setup ───────────────────────────────────────────────── */

static struct wl_event_loop *clip_loop;
struct wl_event_loop *synui_clip_loop(void) { return clip_loop; }

void clipboard_init(syn_server_t *s)
{
    memset(&s->clipboard, 0, sizeof(s->clipboard));
    clip_loop = wl_display_get_event_loop(s->display);

    s->clipboard_set_selection.notify = handle_set_selection;
    wl_signal_add(&s->seat->events.set_selection, &s->clipboard_set_selection);

    wlr_log(WLR_INFO, "synui: clipboard: history up (%d entries, memory only)",
            CLIP_HISTORY_MAX);
}

void clipboard_finish(syn_server_t *s)
{
    wl_list_remove(&s->clipboard_set_selection.link);
    clipboard_clear(s);
}
