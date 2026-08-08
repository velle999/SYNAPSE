/*
 * clip_test.c — the clipboard history actually CAPTURES.
 *
 * This exists because clipboard.c shipped (pkgrel 311) doing nothing at all,
 * and nothing said so. The selection worked perfectly — wlroots owns that, not
 * us — so copy and paste between windows behaved, Super+V opened, and the panel
 * was simply always empty. A feature that is wired up, logs "history up" at
 * startup and silently keeps nothing looks exactly like a feature nobody has
 * copied anything into yet.
 *
 * The bug was one line of event-loop reasoning: clip_read_cb treated
 * WL_EVENT_HANGUP as "the transfer is over" and returned before reading. A
 * client writes its selection and closes the pipe in the same breath, so the
 * kernel reports EPOLLIN|EPOLLHUP in a SINGLE event and libwayland passes both
 * bits at once — the data was there, unread, and thrown away. Every selection
 * small enough to fit a pipe took that path, which is all of them.
 *
 * So the test that matters is the one below: a source that writes and closes
 * exactly as a real client does, driven through the real signal, the real
 * wl_event_loop and the real read path. Anything that stubs the pipe would have
 * passed against the broken code.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>

#include "synui.h"

/* The compositor half. The history is a data structure; drawing it is not what
 * is under test, and a panel that is never shown never asks. */
void synui_render_clipboard(syn_server_t *s) { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *which)
{
    (void)s; (void)which;
}

/* ── A client, as far as the seat is concerned ───────────────
 *
 * WRITE THEN CLOSE, with no dispatch in between. That is not a shortcut, it is
 * the whole point: it is what wl-copy and Qt both do for anything that fits the
 * pipe, and it is the case the old code dropped. */
static const char *payload;
static const char *offer_mime = "text/plain;charset=utf-8";

static void fake_send(struct wlr_data_source *source, const char *mime,
                      int32_t fd)
{
    (void)source;
    if (strcmp(mime, "text/plain;charset=utf-8") != 0) { close(fd); return; }
    size_t len = strlen(payload), off = 0;
    while (off < len) {
        ssize_t n = write(fd, payload + off, len - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
    close(fd);
}

static void fake_destroy(struct wlr_data_source *source) { free(source); }

static const struct wlr_data_source_impl fake_impl = {
    .send    = fake_send,
    .destroy = fake_destroy,
};

static struct wlr_data_source *fake_source(const char *mime)
{
    struct wlr_data_source *src = calloc(1, sizeof(*src));
    assert(src);
    wlr_data_source_init(src, &fake_impl);
    char **p = wl_array_add(&src->mime_types, sizeof(char *));
    assert(p);
    *p = strdup(mime);
    assert(*p);
    return src;
}

static syn_server_t server;   /* static: syn_server_t is far too big for a frame */

/* Offer `text` and let the compositor read it, exactly as a copy would. */
static void copy(const char *text, const char *mime)
{
    payload = text;
    wlr_seat_set_selection(server.seat, fake_source(mime),
                           wl_display_next_serial(server.display));

    /* One turn of the loop is all a real copy gets before the next thing
     * happens. Dispatching until idle would hide a handler that needs a second
     * wakeup to land what it already has — which is the bug. */
    wl_event_loop_dispatch(wl_display_get_event_loop(server.display), 0);
}

static void expect(const char *want, const char *what)
{
    const char *got = clipboard_current_text(&server);
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL: %s\n  want: %s\n  got:  %s\n",
               what, want, got ? got : "(nothing on the clipboard)");
        exit(1);
    }
    printf("  ok  %s\n", what);
}

int main(void)
{
    server.display = wl_display_create();
    assert(server.display);
    server.seat = wlr_seat_create(server.display, "seat0");
    assert(server.seat);

    clipboard_init(&server);

    /* 1. THE REGRESSION. One write, one close, one epoll event carrying
     *    READABLE and HANGUP together. Before the fix this returned nothing. */
    copy("PROBE-FROM-WL-COPY-2", offer_mime);
    expect("PROBE-FROM-WL-COPY-2", "a small selection, written and closed at once");

    /* 2. Bigger than the 4096-byte read chunk, so the loop has to go round
     *    several times before the EOF it commits on. */
    char *big = malloc(10000 + 1);
    assert(big);
    memset(big, 'a', 10000);
    big[10000] = '\0';
    copy(big, offer_mime);
    expect(big, "a selection larger than one read chunk");

    /* 3. A second copy of something else becomes the newest entry, and the
     *    first is still behind it rather than overwritten. */
    copy("second", offer_mime);
    expect("second", "a later copy takes the top");
    assert(server.clipboard.count == 3);
    assert(strcmp(server.clipboard.items[2].text, "PROBE-FROM-WL-COPY-2") == 0);
    printf("  ok  the earlier entries are still in the history\n");

    /* 4. Re-copying what is already on top is not a new entry. */
    copy("second", offer_mime);
    assert(server.clipboard.count == 3);
    printf("  ok  re-copying the top entry does not duplicate it\n");

    /* 5. Not text: an image or a file drag must not enter a text history, and
     *    must not disturb what is in it. */
    copy("<html>", "text/html");
    assert(server.clipboard.count == 3);
    expect("second", "a non-text selection leaves the history alone");

    free(big);
    printf("clip_test: all good\n");
    return 0;
}
