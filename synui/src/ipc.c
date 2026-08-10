/*
 * ipc.c — control socket (the hyprctl of synui)
 *
 * A line-oriented UNIX socket that reports compositor state as JSON and runs
 * any keybind action by name. This is what makes the compositor scriptable:
 * waybar modules, shell scripts and the AI can ask "what's on screen?" and say
 * "switch to desktop 3" without a keyboard.
 *
 *   $ synctl workspaces
 *   $ synctl clients
 *   $ synctl dispatch ws 3
 *   $ synctl dispatch spawn foot
 *
 * Threading: the listener and every client live on the compositor's own
 * wl_event_loop, so command handlers run on the main thread, between frames,
 * with no locking and no chance of racing the scene graph. A client that
 * connects and never speaks costs one fd and nothing else — it is never waited
 * on.
 *
 * Trust: the socket lives in $XDG_RUNTIME_DIR at 0600, so it is reachable only
 * by the user who owns the session. `dispatch spawn` runs commands — but any
 * process that can open this socket already runs as that user and could spawn
 * them directly. It grants nothing new. (This is the same boundary hyprctl and
 * swaymsg operate on.)
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "synui.h"

#define IPC_MAX_CLIENTS  16
#define IPC_CMD_MAX      512

/* ── A growable reply buffer ─────────────────────────────── */
typedef struct {
    char  *buf;
    size_t len, cap;
} ipc_buf_t;

static void bputs(ipc_buf_t *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->len + n + 1) cap *= 2;
        char *nb = realloc(b->buf, cap);
        if (!nb) return;                 /* OOM: the reply is truncated, not lost */
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void bprintf(ipc_buf_t *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    bputs(b, tmp);
}

/* JSON string escaping. Window titles are arbitrary user data — a title with a
 * quote or a backslash in it must not be able to break the document (or forge
 * fields in it). */
static void bjson_str(ipc_buf_t *b, const char *s)
{
    bputs(b, "\"");
    if (!s) { bputs(b, "\""); return; }
    char esc[8];
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  bputs(b, "\\\""); break;
        case '\\': bputs(b, "\\\\"); break;
        case '\n': bputs(b, "\\n");  break;
        case '\r': bputs(b, "\\r");  break;
        case '\t': bputs(b, "\\t");  break;
        default:
            if (*p < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", *p);
                bputs(b, esc);
            } else {
                esc[0] = (char)*p; esc[1] = '\0';
                bputs(b, esc);
            }
        }
    }
    bputs(b, "\"");
}

/* ── State queries ───────────────────────────────────────── */
static const char *layout_name(syn_layout_t l)
{
    switch (l) {
    case LAYOUT_TILING:   return "tiling";
    case LAYOUT_FLOATING: return "floating";
    case LAYOUT_MONOCLE:  return "monocle";
    case LAYOUT_AI:       return "ai";
    case LAYOUT_NIRI:     return "niri";
    case LAYOUT_SPIRAL:   return "spiral";
    case LAYOUT_CASCADE:  return "cascade";
    }
    return "unknown";
}

static void count_buffer(struct wlr_scene_buffer *buffer, int sx, int sy,
                         void *data)
{
    (void)buffer; (void)sx; (void)sy;
    (*(int *)data)++;
}

static void json_view(ipc_buf_t *b, syn_view_t *v)
{
    bputs(b, "{\"app_id\":");
    bjson_str(b, view_app_id(v));
    bputs(b, ",\"title\":");
    bjson_str(b, view_title(v));
    bprintf(b, ",\"workspace\":%d", v->workspace ? v->workspace->index + 1 : 0);
    bputs(b, ",\"output\":");
    bjson_str(b, (v->output && v->output->wlr_output)
                     ? v->output->wlr_output->name : "");
    bprintf(b, ",\"at\":[%d,%d],\"size\":[%d,%d]", v->x, v->y, v->w, v->h);
    bprintf(b, ",\"floating\":%s",   v->floating   ? "true" : "false");
    bprintf(b, ",\"maximized\":%s",  v->maximized  ? "true" : "false");
    bprintf(b, ",\"fullscreen\":%s", v->fullscreen ? "true" : "false");
    bprintf(b, ",\"minimized\":%s",  v->minimized  ? "true" : "false");
    bprintf(b, ",\"xwayland\":%s",   v->is_xwayland ? "true" : "false");
    bprintf(b, ",\"focused\":%s",    v == v->server->focused_view ? "true" : "false");
    bprintf(b, ",\"pid\":%d", (int)view_pid(v));

    /* Why a window can be invisible while every field above says it is fine.
     *
     * Steam wedges intermittently: mapped, IsViewable, responsive — its menus
     * work and games launch — and yet nothing renders. Everything above reports
     * that window as healthy, which is exactly why three sessions of looking at
     * it got nowhere. These three tell the invisibility apart, and a wedge is
     * cleared by the restart that would let us instrument it, so it has to be
     * readable from the *live* compositor:
     *
     *   alpha 0        — anim_fade_in() zeroes alpha and fades up off the frame
     *                    tick; a fade that never ticks leaves a window mapped,
     *                    buffered and perfectly transparent.
     *   enabled false  — something disabled the node (fade-out, occlusion).
     *   buffers 0      — the client genuinely never painted. Steam's own bug.
     *
     * Cheap: the buffer walk is a handful of nodes, and only on request.
     */
    int buffers = 0;
    if (v->mapped && (v->frame || v->scene_tree))
        wlr_scene_node_for_each_buffer(view_node(v), count_buffer, &buffers);
    bprintf(b, ",\"alpha\":%.3f", (double)v->alpha);
    bprintf(b, ",\"fade_active\":%s", v->fade_active ? "true" : "false");
    bprintf(b, ",\"enabled\":%s",
            (v->mapped && (v->frame || v->scene_tree) && view_node(v)->enabled)
                ? "true" : "false");
    bprintf(b, ",\"buffers\":%d", buffers);
    bputs(b, "}");
}

static void cmd_clients(syn_server_t *s, ipc_buf_t *b)
{
    bputs(b, "[");
    int first = 1;
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->mapped) continue;
            if (!first) bputs(b, ",");
            first = 0;
            json_view(b, v);
        }
    }
    bputs(b, "]\n");
}

static void cmd_workspaces(syn_server_t *s, ipc_buf_t *b)
{
    bputs(b, "[");
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_workspace_t *ws = &s->workspaces[w];
        int n = 0;
        syn_view_t *v;
        wl_list_for_each(v, &ws->windows, link)
            if (v->mapped) n++;

        if (w) bputs(b, ",");
        bprintf(b, "{\"id\":%d,\"name\":", w + 1);
        bjson_str(b, ws->name);
        bputs(b, ",\"layout\":");
        bjson_str(b, layout_name(ws->layout));
        bprintf(b, ",\"windows\":%d,\"visible\":%s}",
                n, workspace_visible(ws) ? "true" : "false");
    }
    bputs(b, "]\n");
}

/* Scene-graph accounting. Every mapped view owns exactly one frame under
 * window_tree, so `frames` should equal `views` — anything more is a frame that
 * outlived the view it was built for (the leak that grew one tree per
 * close/restore cycle; see view_frame_destroy). Cheap enough to ask for in a
 * loop, and tests/smoke.sh asserts the equality across re-map cycles. */
static void cmd_scene(syn_server_t *s, ipc_buf_t *b)
{
    int frames = 0;
    struct wlr_scene_node *nd;
    wl_list_for_each(nd, &s->window_tree->children, link)
        frames++;

    int views = 0;
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped && v->frame) views++;
    }

    bprintf(b, "{\"frames\":%d,\"views\":%d,\"leaked\":%d}\n",
            frames, views, frames - views);
}

static void cmd_outputs(syn_server_t *s, ipc_buf_t *b)
{
    bputs(b, "[");
    int first = 1;
    /* The EFFECTIVE primary, not the raw o->primary flag, which is set only by
     * an explicit choice (the display panel's p key, persisted as primary=1 in
     * outputs.conf). Nobody has made that choice on a fresh install, so the flag
     * reported no primary at all while Xwayland demonstrably had one —
     * xwayland_apply_primary() asks this same function, which falls back to the
     * largest enabled output. Reporting the flag was therefore a lie by
     * omission, and an expensive one: WidgetState.qml pins every desktop widget
     * to the primary output's name, so on any desktop where p had never been
     * pressed all of them stayed invisible with their toggles reading "on".
     * `focused` below has always been answered by the function rather than a
     * field for the same reason. */
    syn_output_t *primary = server_primary_output(s);
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        struct wlr_box box;
        output_box_of(s, o, &box);
        if (!first) bputs(b, ",");
        first = 0;
        bputs(b, "{\"name\":");
        bjson_str(b, o->wlr_output->name);
        bprintf(b, ",\"at\":[%d,%d],\"size\":[%d,%d]",
                box.x, box.y, box.width, box.height);
        bprintf(b, ",\"scale\":%.2f", (double)o->wlr_output->scale);
        bprintf(b, ",\"primary\":%s", o == primary ? "true" : "false");
        bprintf(b, ",\"focused\":%s",
                o == server_focused_output(s) ? "true" : "false");
        bputs(b, "}");
    }
    bputs(b, "]\n");
}

/* ── Command dispatch ────────────────────────────────────── */
static void ipc_run(syn_server_t *s, char *line, ipc_buf_t *out)
{
    /* "dispatch <action> [arg…]" — every keybind action, by name. That's the
     * whole point: anything bindable is scriptable, with no second registry to
     * keep in sync. */
    if (strncmp(line, "dispatch", 8) == 0 &&
        (line[8] == ' ' || line[8] == '\0')) {
        char *rest = line[8] ? line + 9 : (char *)"";
        while (*rest == ' ') rest++;
        if (!*rest) { bputs(out, "{\"error\":\"dispatch needs an action\"}\n"); return; }

        char *sp = strchr(rest, ' ');
        const char *arg = "";
        if (sp) { *sp = '\0'; arg = sp + 1; }

        synui_binding_execute(s, rest, arg);
        bputs(out, "{\"ok\":true}\n");
        return;
    }

    /* "calc <expression>" — the answer, plus the panel's memory.
     *
     * Not `dispatch calc`, which toggles the window: this one has a RESULT to
     * return, and dispatch's contract is a bare {"ok":true}. syn-calc(1) is the
     * same parser with no session behind it; this is the one that shares `ans`
     * and the tape with the panel. */
    if (strncmp(line, "calc", 4) == 0 && (line[4] == ' ' || line[4] == '\0')) {
        const char *expr = line[4] ? line + 5 : "";
        while (*expr == ' ') expr++;
        if (!*expr) { bputs(out, "{\"error\":\"calc needs an expression\"}\n"); return; }

        char result[64];
        const char *err = NULL;
        if (!calc_run(s, expr, result, sizeof(result), &err)) {
            bputs(out, "{\"error\":\"");
            bjson_str(out, err ? err : "cannot work that out");
            bputs(out, "\"}\n");
            return;
        }
        /* The result is digits, '.', '-' and 'e' — quoted anyway, because a
         * JSON number is a promise about precision this is not making. */
        bputs(out, "{\"ok\":true,\"result\":\"");
        bjson_str(out, result);
        bputs(out, "\"}\n");
        return;
    }

    if (strcmp(line, "clients") == 0 || strcmp(line, "windows") == 0) {
        cmd_clients(s, out);
        return;
    }
    if (strcmp(line, "workspaces") == 0) {
        cmd_workspaces(s, out);
        return;
    }
    if (strcmp(line, "outputs") == 0 || strcmp(line, "monitors") == 0) {
        cmd_outputs(s, out);
        return;
    }
    if (strcmp(line, "scene") == 0) {
        cmd_scene(s, out);
        return;
    }
    if (strcmp(line, "activeworkspace") == 0) {
        syn_workspace_t *ws = server_active_workspace(s);
        bprintf(out, "{\"id\":%d,\"name\":", ws->index + 1);
        bjson_str(out, ws->name);
        bputs(out, ",\"layout\":");
        bjson_str(out, layout_name(ws->layout));
        bputs(out, "}\n");
        return;
    }
    if (strcmp(line, "activewindow") == 0) {
        if (s->focused_view && s->focused_view->mapped) json_view(out, s->focused_view);
        else                                            bputs(out, "{}");
        bputs(out, "\n");
        return;
    }
    if (strcmp(line, "version") == 0) {
        bputs(out, "{\"compositor\":\"synui\",\"version\":\"" SYNUI_VERSION "\"}\n");
        return;
    }
    if (strcmp(line, "help") == 0) {
        bputs(out, "{\"commands\":[\"clients\",\"workspaces\",\"outputs\","
                   "\"activeworkspace\",\"activewindow\",\"version\","
                   "\"dispatch <action> [arg]\",\"calc <expression>\"]}\n");
        return;
    }

    bputs(out, "{\"error\":\"unknown command; try: help\"}\n");
}

/* ── Socket plumbing ─────────────────────────────────────── */
typedef struct {
    syn_server_t          *server;
    struct wl_event_source *source;
    int                    fd;
    char                   cmd[IPC_CMD_MAX];
    size_t                 len;
} ipc_client_t;

static void ipc_client_close(ipc_client_t *c)
{
    if (c->source) wl_event_source_remove(c->source);
    if (c->fd >= 0) close(c->fd);
    c->server->ipc_clients--;
    free(c);
}

static int ipc_client_readable(int fd, uint32_t mask, void *data)
{
    ipc_client_t *c = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ipc_client_close(c);
        return 0;
    }

    ssize_t n = read(fd, c->cmd + c->len, sizeof(c->cmd) - 1 - c->len);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        ipc_client_close(c);
        return 0;
    }
    c->len += (size_t)n;
    c->cmd[c->len] = '\0';

    char *nl = strchr(c->cmd, '\n');
    if (!nl) {
        /* No newline and the buffer is full: a client shouting nonsense. Drop
         * it rather than growing without bound. */
        if (c->len >= sizeof(c->cmd) - 1)
            ipc_client_close(c);
        return 0;
    }
    *nl = '\0';

    ipc_buf_t out = {0};
    ipc_run(c->server, c->cmd, &out);
    if (out.buf) {
        /* Best-effort: a client that hung up mid-reply is its own problem, and
         * must never block the compositor. */
        ssize_t unused = write(fd, out.buf, out.len);
        (void)unused;
        free(out.buf);
    }
    ipc_client_close(c);
    return 0;
}

static int ipc_accept(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (cfd < 0) return 0;

    if (s->ipc_clients >= IPC_MAX_CLIENTS) {
        close(cfd);                 /* a runaway script must not exhaust our fds */
        return 0;
    }

    ipc_client_t *c = calloc(1, sizeof(*c));
    if (!c) { close(cfd); return 0; }
    c->server = s;
    c->fd     = cfd;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    c->source = wl_event_loop_add_fd(loop, cfd, WL_EVENT_READABLE,
                                     ipc_client_readable, c);
    s->ipc_clients++;
    return 0;
}

void ipc_setup(syn_server_t *s)
{
    s->ipc_fd = -1;

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime) {
        wlr_log(WLR_ERROR, "synui: ipc: no XDG_RUNTIME_DIR, control socket disabled");
        return;
    }

    /* One socket per compositor instance: keyed on the Wayland display so a
     * nested/headless synui can't collide with the session one. */
    const char *disp = getenv("WAYLAND_DISPLAY");
    snprintf(s->ipc_path, sizeof(s->ipc_path), "%s/synui-%s.sock",
             runtime, disp ? disp : "0");

    unlink(s->ipc_path);            /* a stale socket from a crash would bind-fail */

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "synui: ipc: socket: %s", strerror(errno));
        return;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (strlen(s->ipc_path) >= sizeof(addr.sun_path)) {
        wlr_log(WLR_ERROR, "synui: ipc: path too long: %s", s->ipc_path);
        close(fd);
        return;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", s->ipc_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 8) < 0) {
        wlr_log(WLR_ERROR, "synui: ipc: bind/listen: %s", strerror(errno));
        close(fd);
        s->ipc_path[0] = '\0';
        return;
    }

    /* Session-owner only. XDG_RUNTIME_DIR is already 0700, but a socket that
     * grants "run any command" should not rely on the directory alone. */
    chmod(s->ipc_path, 0600);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->ipc_source = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                                         ipc_accept, s);
    s->ipc_fd = fd;

    /* Children inherit it, so `synctl` needs no arguments. */
    setenv("SYNUI_SOCKET", s->ipc_path, 1);
    wlr_log(WLR_INFO, "synui: ipc: listening on %s", s->ipc_path);
}

void ipc_destroy(syn_server_t *s)
{
    if (s->ipc_source) { wl_event_source_remove(s->ipc_source); s->ipc_source = NULL; }
    if (s->ipc_fd >= 0) { close(s->ipc_fd); s->ipc_fd = -1; }
    if (s->ipc_path[0]) { unlink(s->ipc_path); s->ipc_path[0] = '\0'; }
}
