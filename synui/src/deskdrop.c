/*
 * deskdrop.c — dropping a drag-and-drop onto the desktop.
 *
 * Dragging a file out of Dolphin (or anything else) and letting go over the
 * wallpaper used to do nothing at all, and it could not have done anything: a
 * Wayland drop is delivered to the wl_surface under the cursor, and the desktop
 * is not a surface. It is wallpaper and a cairo buffer of icons that synui
 * paints itself (deskmenu.c owns the model, render.c draws it), so the drag had
 * no target there and wlroots refused the drop.
 *
 * So the compositor becomes the target. It never gets a wl_data_offer — those
 * only go to clients — but it owns the seat, so it can talk to the drag's
 * wlr_data_source directly, which is all a target actually does:
 *
 *   hover   wlr_data_source_accept(src, "text/uri-list") + dnd_action(COPY).
 *           That is what turns the source's cursor from "no" into "copy" and
 *           what tells it a drop here will be honoured.
 *   drop    wlr_data_source_dnd_drop(), then _send() down a pipe we read from
 *           the wl_event_loop, then _dnd_finish() once the URIs are in.
 *
 * THE RELEASE HAS TO BE INTERCEPTED BEFORE wlroots SEES IT. wlroots' drag grab
 * (drag_handle_pointer_button) reads a release with no *client* drag focus as a
 * failed drop and calls wlr_data_source_destroy() — which sends the source
 * wl_data_source.cancelled, and a cancelled source abandons the transfer we
 * just asked it for. input.c's pointer_button() therefore runs deskdrop_take()
 * first and then ends the drag itself with wlr_seat_pointer_end_grab(): that
 * tears the drag down and leaves the source alone.
 *
 * ONLY SOURCES THAT SET ACTIONS ARE TAKEN, and that is not a preference — it is
 * a crash. wlroots' client_data_source_dnd_drop/_dnd_finish/_dnd_action all
 * assert() the wl_data_source is at least version 3, and a v1/v2 source would
 * abort the compositor from inside our own drop path. wl_data_source.set_actions
 * arrived in the same version, so `actions >= 0` (the field is -1 until the
 * client sets it) is exactly the version test, expressed in something public.
 * wlroots relies on the same implication for client-to-client drops.
 *
 * A DROP IS ALWAYS A COPY. MOVE is offered by every file manager and it would
 * be one more flag here — but in this protocol the *source* is what deletes the
 * original, and it does so when it sees dnd_finished. We would be promising a
 * file is safely on the desktop before `cp` has finished writing it, with no
 * exit status to check (SIGCHLD belongs to reap_children()). Copying is the
 * version of this feature that cannot lose a file.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "synui.h"

/* The one flavour a file drag has in common everywhere — file managers, Firefox,
 * GTK and Qt all offer it, and it is the only one that names files. */
#define DROP_MIME       "text/uri-list"

/* A uri-list is a few hundred bytes of text. Anything past this is a client
 * offering us something that is not a uri-list, and the compositor is not the
 * place to hold it. */
#define DROP_LIST_MAX   (64 * 1024)

#define DROP_FILES_MAX  64
#define DROP_NAME_MAX   256

/* An in-flight read of the dropped uri-list. */
struct drop_read {
    struct wl_list          link;
    syn_server_t           *server;
    /* The source, so dnd_finish() can be sent when the transfer is done. NULL
     * the moment it dies — the client may destroy it mid-read and we would have
     * no other way to know. */
    struct wlr_data_source *source;
    struct wl_listener      source_destroy;
    struct wl_event_source *ev;
    int                     fd;
    int                     x, y;        /* where it was dropped, layout coords */
    char                   *buf;
    size_t                  len;
};

/* One drop's `cp` children, watched to completion. */
struct drop_watch {
    struct wl_list          link;
    syn_server_t           *server;
    struct wl_event_source *ev;
    int                     fd;
    int                     x, y;
    int                     n;
    char                    names[DROP_FILES_MAX][DROP_NAME_MAX];
};

static struct wl_list drop_reads  = { &drop_reads,  &drop_reads  };
static struct wl_list drop_watches = { &drop_watches, &drop_watches };

/* The source we have told "yes, drop here", so the hover only goes on the wire
 * when the answer changes. COMPARED, NEVER DEREFERENCED: it belongs to a client
 * that can destroy it without telling us, and the live one is always reachable
 * as s->seat->drag->source anyway. */
static struct wlr_data_source *accepted_src;

/* ── Hover ───────────────────────────────────────────────── */

static bool source_offers(struct wlr_data_source *src, const char *mime)
{
    char **m;
    wl_array_for_each(m, &src->mime_types)
        if (strcmp(*m, mime) == 0) return true;
    return false;
}

void deskdrop_reset(syn_server_t *s)
{
    (void)s;
    accepted_src = NULL;
}

void deskdrop_hover(syn_server_t *s, bool over_desktop)
{
    struct wlr_drag *drag = s->seat->drag;
    if (!drag || !drag->source) { accepted_src = NULL; return; }

    struct wlr_data_source *src = drag->source;

    /* With the icons off the desktop is wallpaper and nothing else, so there is
     * nowhere for a dropped file to appear. Refusing the drop is the honest
     * answer — the source keeps its "no drop" cursor — where taking it would
     * copy files into a ~/Desktop the user cannot see. */
    bool take = over_desktop &&
                s->config.desktop_icons &&
                src->actions >= 0 &&        /* == "this source is v3+"; see top */
                (src->actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) &&
                source_offers(src, DROP_MIME);

    if (take) {
        if (accepted_src == src) return;
        accepted_src = src;
        wlr_data_source_accept(src, 0, DROP_MIME);
        wlr_data_source_dnd_action(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    } else if (accepted_src == src) {
        /* Off the desktop and onto a window. Withdraw before the caller hands
         * drag focus to that client, or our acceptance is still standing when
         * its offer arrives and wlroots would drop into a client that never
         * agreed to take it. */
        accepted_src = NULL;
        wlr_data_source_accept(src, 0, NULL);
        wlr_data_source_dnd_action(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
    }
}

/* ── file:// URIs ────────────────────────────────────────── */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * "file:///home/velle/a%20file" → "/home/velle/a file".
 *
 * false for anything that is not a local file: an http URL (a link drag), a
 * file:// on another host, a path with a NUL in it, or one too long for `out`.
 * Refusing beats truncating — a truncated path is a different file.
 */
static bool uri_to_path(const char *uri, size_t len, char *out, size_t n)
{
    if (len < 8 || strncasecmp(uri, "file://", 7) != 0) return false;
    const char *p = uri + 7;
    len -= 7;

    /* The authority. Empty ("file:///…") or "localhost" is this machine; a real
     * host is a file we have no way to read. */
    const char *slash = memchr(p, '/', len);
    if (!slash) return false;
    size_t host = (size_t)(slash - p);
    if (host != 0 && !(host == 9 && strncasecmp(p, "localhost", 9) == 0))
        return false;
    p    = slash;
    len -= host;

    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        int c = (unsigned char)p[i];
        if (c == '%' && i + 2 < len) {
            int hi = hexval(p[i + 1]), lo = hexval(p[i + 2]);
            if (hi >= 0 && lo >= 0) { c = hi * 16 + lo; i += 2; }
        }
        if (c == '\0') return false;
        if (o + 1 >= n) return false;
        out[o++] = (char)c;
    }
    out[o] = '\0';
    return o > 1 && out[0] == '/';
}

/*
 * Walk a uri-list one candidate at a time.
 *
 * The format is CRLF-terminated by spec and LF-terminated in practice, so both
 * are accepted, and either may be the last thing in the buffer or not there at
 * all. '#' introduces a comment and blank lines are padding — neither ever
 * reaches the caller, so everything handed back is something to try to open.
 */
static bool uri_list_next(char **cursor, const char **line, size_t *len)
{
    while (*cursor && **cursor) {
        char  *start = *cursor;
        char  *end   = strpbrk(start, "\r\n");
        size_t n     = end ? (size_t)(end - start) : strlen(start);

        *cursor = end;
        while (*cursor && (**cursor == '\r' || **cursor == '\n')) (*cursor)++;

        if (n && start[0] != '#') { *line = start; *len = n; return true; }
    }
    return false;
}

/* The last component of a path, with any trailing slashes taken off first —
 * "/home/velle/Music/" names Music, not "". NULL if nothing is left. */
static const char *path_base(char *path)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') path[--n] = '\0';
    if (n == 0 || strcmp(path, "/") == 0) return NULL;
    char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    return *base ? base : NULL;
}

/*
 * Where `base` may be written inside `dir` without overwriting anything.
 *
 * A drop must never clobber: dragging photo.jpg onto a desktop that already has
 * one is a second file, not a replacement, and a `cp` that silently ate the
 * first would be unrecoverable. Collisions become "photo (copy).jpg",
 * "photo (copy 2).jpg", … the way every other shell spells it.
 */
static bool dest_for(const char *dir, const char *base, char *out, size_t n)
{
    struct stat st;
    if (snprintf(out, n, "%s/%s", dir, base) >= (int)n) return false;
    if (lstat(out, &st) != 0) return true;

    /* Split the extension so the suffix lands before ".jpg", not after it. A
     * leading dot is part of the name (".bashrc"), not an extension. */
    const char *dot = strrchr(base, '.');
    int stem = (dot && dot != base) ? (int)(dot - base) : (int)strlen(base);
    const char *ext = base + stem;

    for (int i = 1; i < 100; i++) {
        int w = (i == 1)
            ? snprintf(out, n, "%s/%.*s (copy)%s", dir, stem, base, ext)
            : snprintf(out, n, "%s/%.*s (copy %d)%s", dir, stem, base, i, ext);
        if (w >= (int)n) return false;
        if (lstat(out, &st) != 0) return true;
    }
    return false;
}

/*
 * Copy one path into place, in a child.
 *
 * `cp -a` rather than an open/read/write loop here: the compositor's event loop
 * is the same one every client draws on, and a drop of a 4GB video would freeze
 * the whole session for as long as the write took. -T because `dst` is the full
 * destination name we already resolved, not a directory to copy into.
 *
 * `watch_fd` is deliberately inherited across the exec (its FD_CLOEXEC is
 * cleared here, and only here, so no other spawn in this tree picks it up):
 * `cp` holding it is what makes the read end hang up when the last copy is
 * done, which is how the icons get rescanned without a waitpid() racing
 * reap_children() for the status.
 */
static bool fork_copy(const char *src, const char *dst, int watch_fd)
{
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        fcntl(watch_fd, F_SETFD, 0);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execlp("cp", "cp", "-a", "-T", "--", src, dst, (char *)NULL);
        _exit(127);
    }
    return true;
}

/* ── Committing a drop ───────────────────────────────────── */

static void watch_finish(struct drop_watch *w)
{
    wl_list_remove(&w->link);
    if (w->ev) wl_event_source_remove(w->ev);
    if (w->fd >= 0) close(w->fd);
    free(w);
}

/*
 * Every `cp` has exited (nothing holds the write end any more), so the files
 * are on disk and the desktop can be rescanned. There is no inotify watch on
 * ~/Desktop — a reload is the only thing that ever makes a new file appear —
 * and the reload has to come first, because the model cannot be told where to
 * put a file it does not know about yet.
 */
static int watch_cb(int fd, uint32_t mask, void *data)
{
    struct drop_watch *w = data;
    (void)fd; (void)mask;

    deskicons_reload(w->server);

    const char *names[DROP_FILES_MAX];
    for (int i = 0; i < w->n; i++) names[i] = w->names[i];
    deskicons_place_dropped(w->server, names, w->n, w->x, w->y);

    watch_finish(w);
    return 0;
}

/* ~/Desktop, created if the drop is the first thing to need it. */
static bool desktop_dir(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    if (snprintf(buf, n, "%s/Desktop", home) >= (int)n) return false;
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return false;
    return true;
}

/* Walk the uri-list, start a copy per local file, and arrange for the desktop
 * to be rescanned once they have all finished. */
static void drop_commit(struct drop_read *r)
{
    syn_server_t *s = r->server;

    char dir[512];
    if (!desktop_dir(dir, sizeof(dir))) {
        wlr_log(WLR_ERROR, "synui: deskdrop: no usable ~/Desktop");
        return;
    }

    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0) return;
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);

    struct drop_watch *w = calloc(1, sizeof(*w));
    if (!w) { close(pfd[0]); close(pfd[1]); return; }
    w->server = s;
    w->fd     = pfd[0];
    w->x      = r->x;
    w->y      = r->y;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    w->ev = wl_event_loop_add_fd(loop, pfd[0], WL_EVENT_READABLE, watch_cb, w);
    if (!w->ev) { close(pfd[0]); close(pfd[1]); free(w); return; }
    wl_list_insert(&drop_watches, &w->link);

    int taken = 0, refused = 0;
    char       *cursor = r->buf;
    const char *line;
    size_t      len;

    while (uri_list_next(&cursor, &line, &len)) {
        char src[PATH_MAX], tmp[PATH_MAX], dst[PATH_MAX];
        const char *base = NULL;

        if (uri_to_path(line, len, src, sizeof(src))) {
            snprintf(tmp, sizeof(tmp), "%s", src);
            base = path_base(tmp);
        }

        if (!base || !dest_for(dir, base, dst, sizeof(dst)) ||
            !fork_copy(src, dst, pfd[1])) {
            refused++;
        } else {
            taken++;
            /* Remember what `cp` is creating — not always the name it came
             * from, since dest_for() de-duplicates — so the drop point can be
             * pinned once the copy lands. A name too long to record is still
             * copied; it just flows to a free cell like any other new file,
             * which beats recording a truncated one that would match nothing
             * and place nothing. */
            const char *slash = strrchr(dst, '/');
            const char *made  = slash ? slash + 1 : dst;
            if (w->n < DROP_FILES_MAX && strlen(made) < DROP_NAME_MAX)
                memcpy(w->names[w->n++], made, strlen(made) + 1);
        }

        if (taken + refused >= DROP_FILES_MAX) break;
    }

    /* Our own end goes now: from here the children are the only writers, so the
     * hangup means all of them are done. With none forked it fires straight
     * away, which harmlessly rescans a desktop nothing was added to. */
    close(pfd[1]);

    if (taken == 0)
        notif_post(s, "Desktop", "Nothing to drop here",
                   refused ? "That drag carried no local files."
                           : "That drag carried nothing this desktop can hold.",
                   NOTIF_URGENCY_NORMAL, -1, 0);
    else
        wlr_log(WLR_DEBUG, "synui: deskdrop: copying %d file(s) to %s",
                taken, dir);
}

/* ── Reading the uri-list ────────────────────────────────── */

static void read_finish(struct drop_read *r)
{
    /* The transfer is over either way, and the source is owed a dnd_finished:
     * until it gets one the client keeps the drag alive on its side (a cursor
     * that never comes back, in GTK). It can also destroy the source the moment
     * it is sent, which is why nothing touches it afterwards. */
    if (r->source) {
        struct wlr_data_source *src = r->source;
        wl_list_remove(&r->source_destroy.link);
        r->source = NULL;
        wlr_data_source_dnd_finish(src);
    }

    wl_list_remove(&r->link);
    if (r->ev) wl_event_source_remove(r->ev);
    if (r->fd >= 0) close(r->fd);
    free(r->buf);
    free(r);
}

static void read_done(struct drop_read *r)
{
    if (r->len) {
        r->buf[r->len] = '\0';
        drop_commit(r);
    } else {
        /* Zero bytes means the source was asked and answered with nothing —
         * and this used to return in silence, which is the worst possible
         * outcome: the drop was accepted, the cursor came home, and the file
         * simply never appeared. Every other refusal in this file says so;
         * this one has to as well, or the next person debugging it has no
         * evidence that the drop even happened. */
        wlr_log(WLR_ERROR, "synui: deskdrop: the source sent no uri-list");
        notif_post(r->server, "Desktop", "The drop arrived empty",
                   "That application offered a file list and then sent nothing.",
                   NOTIF_URGENCY_NORMAL, -1, 0);
    }
    read_finish(r);
}

static int read_cb(int fd, uint32_t mask, void *data)
{
    struct drop_read *r = data;

    /* A hangup with bytes already in hand is the normal end of a short
     * transfer, not a failure — the client wrote the list and closed. */
    if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) { read_done(r); return 0; }

    for (;;) {
        if (r->len >= DROP_LIST_MAX) {
            wlr_log(WLR_ERROR, "synui: deskdrop: uri-list over %d bytes — dropped",
                    DROP_LIST_MAX);
            read_finish(r);
            return 0;
        }

        size_t space = DROP_LIST_MAX - r->len;
        if (space > 4096) space = 4096;
        char *nb = realloc(r->buf, r->len + space + 1);
        if (!nb) { read_finish(r); return 0; }
        r->buf = nb;

        ssize_t n = read(fd, r->buf + r->len, space);
        if (n > 0) { r->len += (size_t)n; continue; }
        if (n == 0) { read_done(r); return 0; }        /* EOF: the whole list */
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) continue;
        read_finish(r);
        return 0;
    }
}

static void handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct drop_read *r = wl_container_of(listener, r, source_destroy);
    (void)data;

    /* wlr_data_source_destroy() asserts this list is empty once it has emitted,
     * so the removal is not optional. */
    wl_list_remove(&r->source_destroy.link);
    wl_list_init(&r->source_destroy.link);
    r->source = NULL;
}

bool deskdrop_take(syn_server_t *s, double lx, double ly)
{
    struct wlr_drag *drag = s->seat->drag;
    if (!drag || !drag->source || accepted_src != drag->source) return false;

    struct wlr_data_source *src = drag->source;
    accepted_src = NULL;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return false;

    struct drop_read *r = calloc(1, sizeof(*r));
    if (!r) { close(fds[0]); close(fds[1]); return false; }
    r->server = s;
    r->fd     = fds[0];
    r->x      = (int)lx;
    r->y      = (int)ly;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    r->ev = wl_event_loop_add_fd(loop, fds[0], WL_EVENT_READABLE, read_cb, r);
    if (!r->ev) { close(fds[0]); close(fds[1]); free(r); return false; }
    wl_list_insert(&drop_reads, &r->link);

    r->source = src;
    r->source_destroy.notify = handle_source_destroy;
    wl_signal_add(&src->events.destroy, &r->source_destroy);

    /* The drop first, then the request for the data — the order a client target
     * would produce, since it only asks through the offer it is handed with the
     * drop. wlr_data_source_send() closes the write end for us; without that
     * the read end would never see EOF. */
    wlr_data_source_dnd_drop(src);
    wlr_data_source_send(src, DROP_MIME, fds[1]);
    return true;
}

void deskdrop_finish(syn_server_t *s)
{
    (void)s;
    accepted_src = NULL;

    struct drop_read *r, *rt;
    wl_list_for_each_safe(r, rt, &drop_reads, link)
        read_finish(r);

    struct drop_watch *w, *wt;
    wl_list_for_each_safe(w, wt, &drop_watches, link)
        watch_finish(w);
}
