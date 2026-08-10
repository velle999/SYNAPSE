/*
 * deskdrag.c — dragging a file OFF the desktop, into a window.
 *
 * deskdrop.c is the other direction: a client drags a file and the desktop
 * takes it. This is the desktop as the SOURCE, which is a stranger job,
 * because the thing being dragged does not belong to any client. The
 * compositor owns ~/Desktop's icons, so the compositor has to be the
 * wl_data_source — the same shape wlroots gives a client's source, with our
 * own send() writing the file's URI down the pipe.
 *
 * WHEN a reposition becomes a drag is the whole design. Inside the desktop, a
 * dragged icon is being moved to a cell and nothing else — that is deskmenu.c's
 * gesture and it must keep working exactly as it did. So the promotion happens
 * at the moment the cursor leaves the desktop and enters a window: the icon
 * snaps back to where it started (it is not being repositioned any more) and a
 * real Wayland drag begins under the same button press. Let go over the
 * desktop again and nothing has happened, which is the right answer for a
 * gesture that ended where it began.
 *
 * A DRAG OUT IS ALWAYS A COPY, for the same reason a drop onto the desktop is
 * (see deskdrop.c): MOVE means the SOURCE deletes the original when the target
 * reports finished, and this source would be deleting a user's file on the word
 * of a program it cannot check. Copying is the version of this that cannot
 * lose a file.
 *
 * wlr_drag_create() needs a wlr_seat_client and dereferences it, so a drag with
 * no client behind it still has to borrow one. Any of them will do — with a
 * non-NULL source wlroots only uses it to reach ->seat — but a borrowed client
 * can disconnect mid-drag and leave that pointer dangling, so its destroy
 * signal is watched and takes the drag down with it.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "synui.h"

#define DRAG_MIME_URIS "text/uri-list"
#define DRAG_MIME_TEXT "text/plain"

/* ── The URI ─────────────────────────────────────────────── */

/*
 * Percent-encode a path into a file:// URI.
 *
 * Everything outside the unreserved set goes, and the separator stays — the
 * same rule every file manager's uri-list follows, and the inverse of
 * deskdrop.c's uri_to_path(), which is what makes a file dragged out of one
 * desktop and back onto another survive the round trip. Spaces and the letters
 * of a name in any language are the common case; getting this wrong hands the
 * receiving program a different filename, not an error.
 */
bool deskdrag_uri_for(const char *path, char *out, size_t n)
{
    static const char *unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~/";

    if (!path || !*path || *path != '/') return false;

    size_t w = 0;
    const char *prefix = "file://";
    for (const char *p = prefix; *p; p++) {
        if (w + 1 >= n) return false;
        out[w++] = *p;
    }

    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (strchr(unreserved, (char)*p)) {
            if (w + 1 >= n) return false;
            out[w++] = (char)*p;
        } else {
            if (w + 3 >= n) return false;
            static const char hex[] = "0123456789ABCDEF";
            out[w++] = '%';
            out[w++] = hex[*p >> 4];
            out[w++] = hex[*p & 0x0f];
        }
    }
    if (w + 1 >= n) return false;
    out[w] = '\0';
    return true;
}

/* ── The source ──────────────────────────────────────────── */

struct desk_source {
    struct wlr_data_source  base;
    char                    path[PATH_MAX];
    char                    uri[PATH_MAX * 3 + 16];
    struct wl_listener      seat_client_destroy;
};

/* The one live drag out of the desktop, or NULL. deskdrop.c asks, so a drag
 * dropped back onto the desktop is not copied onto itself. */
static struct desk_source *current;

static void desk_source_send(struct wlr_data_source *source,
                             const char *mime_type, int32_t fd)
{
    struct desk_source *src = (struct desk_source *)source;

    const char *body = NULL;
    char        line[sizeof(src->uri) + 2];

    if (strcmp(mime_type, DRAG_MIME_URIS) == 0) {
        snprintf(line, sizeof(line), "%s\r\n", src->uri);
        body = line;
    } else if (strcmp(mime_type, DRAG_MIME_TEXT) == 0) {
        body = src->path;
    }

    if (body) {
        size_t len = strlen(body), off = 0;
        while (off < len) {
            ssize_t k = write(fd, body + off, len - off);
            if (k <= 0) break;
            off += (size_t)k;
        }
    }
    close(fd);
}

static void desk_source_accept(struct wlr_data_source *source, uint32_t serial,
                               const char *mime_type)
{
    (void)source; (void)serial; (void)mime_type;
}

static void desk_source_destroy(struct wlr_data_source *source)
{
    struct desk_source *src = (struct desk_source *)source;
    if (current == src) current = NULL;
    wl_list_remove(&src->seat_client_destroy.link);
    free(src);
}

/* The target took it. Nothing to do: this source never deletes anything, so
 * there is no "the move completed" work waiting on either of these. */
static void desk_source_dnd_drop(struct wlr_data_source *source) { (void)source; }
static void desk_source_dnd_finish(struct wlr_data_source *source) { (void)source; }
static void desk_source_dnd_action(struct wlr_data_source *source,
                                   enum wl_data_device_manager_dnd_action action)
{
    (void)source; (void)action;
}

static const struct wlr_data_source_impl desk_source_impl = {
    .send       = desk_source_send,
    .accept     = desk_source_accept,
    .destroy    = desk_source_destroy,
    .dnd_drop   = desk_source_dnd_drop,
    .dnd_finish = desk_source_dnd_finish,
    .dnd_action = desk_source_dnd_action,
};

static void handle_seat_client_destroy(struct wl_listener *listener, void *data)
{
    struct desk_source *src =
        wl_container_of(listener, src, seat_client_destroy);
    (void)data;

    /* wlr_drag holds this client without watching it, and dereferences it on
     * every focus change. Taking the source down takes the drag with it. */
    wl_list_remove(&src->seat_client_destroy.link);
    wl_list_init(&src->seat_client_destroy.link);
    wlr_data_source_destroy(&src->base);
}

bool deskdrag_is_ours(struct wlr_data_source *source)
{
    return current != NULL && source == &current->base;
}

/* ── Starting it ─────────────────────────────────────────── */

bool deskdrag_start(syn_server_t *s, const char *path)
{
    if (!s || !s->seat || s->seat->drag || current) return false;
    if (!path || !*path) return false;

    /* Any seat client will do; wlroots needs one only to reach ->seat. The
     * client under the cursor is the natural pick — it is the one this drag is
     * heading for, so it is the one most likely to outlive the gesture. */
    struct wlr_seat_client *sc = NULL;
    double sx, sy;
    syn_view_t *view = NULL;
    struct wlr_surface *surface =
        surface_at(s, s->cursor->x, s->cursor->y, &view, &sx, &sy);
    if (surface)
        sc = wlr_seat_client_for_wl_client(s->seat,
                                           wl_resource_get_client(surface->resource));
    if (!sc) return false;

    struct desk_source *src = calloc(1, sizeof(*src));
    if (!src) return false;

    snprintf(src->path, sizeof(src->path), "%s", path);
    if (!deskdrag_uri_for(src->path, src->uri, sizeof(src->uri))) {
        free(src);
        return false;
    }

    wlr_data_source_init(&src->base, &desk_source_impl);

    /* COPY only, and stated: a target that sees no action offers no drop, and
     * a target that sees MOVE would expect the original to disappear. */
    src->base.actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;

    const char *mimes[] = { DRAG_MIME_URIS, DRAG_MIME_TEXT };
    for (size_t i = 0; i < sizeof mimes / sizeof *mimes; i++) {
        char **m = wl_array_add(&src->base.mime_types, sizeof(char *));
        if (!m) { wlr_data_source_destroy(&src->base); return false; }
        *m = strdup(mimes[i]);
        if (!*m) { wlr_data_source_destroy(&src->base); return false; }
    }

    wl_list_init(&src->seat_client_destroy.link);
    src->seat_client_destroy.notify = handle_seat_client_destroy;
    wl_signal_add(&sc->events.destroy, &src->seat_client_destroy);

    struct wlr_drag *drag = wlr_drag_create(sc, &src->base, NULL);
    if (!drag) {
        wlr_data_source_destroy(&src->base);
        return false;
    }

    current = src;
    wlr_seat_start_pointer_drag(s->seat, drag, s->seat->pointer_state.grab_serial);
    wlr_log(WLR_DEBUG, "synui: deskdrag: dragging %s out of the desktop",
            src->path);
    return true;
}
