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
 *   $ synctl recent
 *   $ synctl dispatch ws 3
 *   $ synctl dispatch spawn foot
 *
 * `clients` and `recent` are the two halves of the same question and it is
 * worth saying which is which: clients is what is OPEN, asked of the scene
 * graph; recent is what has been opened, kept by recent.c because a window
 * mapping is the only event every way of launching something has in common.
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
    /* Edge-expand, one flag per axis. Reported for the same reason `stack` and
     * `enabled` below are: nothing else can answer it. An expanded window is
     * floating and un-maximized and sits at some box — exactly what an ordinary
     * hand-resized window looks like from out here — so without these two,
     * "did the double-click take" and "did the user drag the edge" produce
     * identical output. tests/edge_expand.sh is the reason they exist. */
    bprintf(b, ",\"expand_v\":%s",
            (v->expanded & SYN_EXPAND_V) ? "true" : "false");
    bprintf(b, ",\"expand_h\":%s",
            (v->expanded & SYN_EXPAND_H) ? "true" : "false");
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
     *   alpha 0        — anim_window_open() zeroes alpha and fades up off the frame
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
    /* Where the frame is being DRAWN, relative to the "at" above, while an
     * animation displaces it — the rise of an opening window, the slide of a
     * desktop leaving. "at" stays the logical geometry throughout, which is
     * what every other consumer wants and is also why the displacement is
     * otherwise unobservable: without this, a slide and a teleport produce
     * identical output here. tests/ws_slide.sh is the reason it exists. */
    bprintf(b, ",\"anim_offset\":[%d,%d]", v->anim_dx, v->anim_dy);
    bprintf(b, ",\"enabled\":%s",
            (v->mapped && (v->frame || v->scene_tree) && view_node(v)->enabled)
                ? "true" : "false");
    bprintf(b, ",\"buffers\":%d", buffers);

    /* Which window is IN FRONT. Index among window_tree's children: 0 is the
     * bottom of the stack and the highest number is what the user is looking
     * at (wlr_scene keeps the list bottom-first — raise_to_top places a node
     * above children.prev, the tail). -1 means the node is not a direct child
     * of window_tree, which a mapped view never is.
     *
     * Added because nothing else could answer it, and z-order is not cosmetic:
     * a layout that quietly restacks the desktop reads as "my window keeps
     * disappearing behind the others" and is invisible to every field above,
     * exactly as `enabled` was for the wedge. See tests/cascade_focus_top.sh. */
    int stack = -1, zi = 0;
    struct wlr_scene_node *nd;
    wl_list_for_each(nd, &v->server->window_tree->children, link) {
        if (nd == view_node(v)) { stack = zi; break; }
        zi++;
    }
    bprintf(b, ",\"stack\":%d", stack);
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

/* Which mapped window owns a surface, or NULL. Diagnostic-only: the pointer
 * probe below reports surfaces by the window a person can name. */
static syn_view_t *ipc_view_of_surface(syn_server_t *s, struct wlr_surface *surf)
{
    if (!surf) return NULL;
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped && view_surface(v) == surf) return v;
    }
    return NULL;
}

static void json_surface_owner(ipc_buf_t *b, syn_server_t *s,
                               struct wlr_surface *surf)
{
    syn_view_t *v = ipc_view_of_surface(s, surf);
    if (!v) { bputs(b, surf ? "\"(not a window)\"" : "null"); return; }
    bjson_str(b, view_app_id(v));
}

static void json_constraint(ipc_buf_t *b, syn_server_t *s,
                            struct wlr_pointer_constraint_v1 *c)
{
    bputs(b, "{\"type\":");
    bjson_str(b, c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED
                     ? "locked" : "confined");
    bputs(b, ",\"lifetime\":");
    bjson_str(b, c->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT
                     ? "oneshot" : "persistent");
    bputs(b, ",\"surface\":");
    json_surface_owner(b, s, c->surface);
    bprintf(b, ",\"active\":%s",
            s->active_constraint == c ? "true" : "false");
    /* The region is in SURFACE coordinates and an empty one is meaningful:
     * a confinement with nothing to confine to does not bind at all. */
    if (pixman_region32_not_empty(&c->region)) {
        pixman_box32_t *e = pixman_region32_extents(&c->region);
        bprintf(b, ",\"region\":[%d,%d,%d,%d]",
                e->x1, e->y1, e->x2 - e->x1, e->y2 - e->y1);
    } else {
        bputs(b, ",\"region\":null");
    }
    bputs(b, "}");
}

/*
 * `synctl pointer` — who is holding the mouse, and inside what.
 *
 * ⚠ A CURSOR POSITION ALONE CANNOT ANSWER THIS, and three sessions were spent
 * proving it. A frozen cursor is a working lock and a moving one is not always
 * a broken one; a game that never asked for a lock and a game whose lock was
 * destroyed look identical from `synctl cursor`, and so do "confined to the
 * screen" and "confined to the game's picture". Every one of those is a
 * different bug with a different fix, so they are reported apart here:
 *
 *   constraints  every constraint on file, whether or not it is active. This
 *                is the one that says whether the CLIENT ever asked — an empty
 *                list means no amount of work in constraints.c can help.
 *   active       the one binding right now. `bound` is the sharper question:
 *                a constraint only binds while its surface holds POINTER
 *                focus, so an active constraint whose surface does not is a
 *                lock that has silently stopped locking.
 *   game         the compositor's own confinement, which needs no client
 *                request and answers to game mode alone.
 */
static void cmd_pointer(syn_server_t *s, ipc_buf_t *b)
{
    struct wlr_surface *pfocus = s->seat->pointer_state.focused_surface;

    bprintf(b, "{\"cursor\":[%.3f,%.3f]", s->cursor->x, s->cursor->y);
    bprintf(b, ",\"seat_surface_xy\":[%.3f,%.3f]",
            s->seat->pointer_state.sx, s->seat->pointer_state.sy);
    bprintf(b, ",\"buttons\":%u", s->seat->pointer_state.button_count);
    bprintf(b, ",\"smoothing\":%d", s->config.pointer_smoothing);

    bputs(b, ",\"pointer_focus\":");
    json_surface_owner(b, s, pfocus);
    bputs(b, ",\"keyboard_focus\":");
    if (s->focused_view && s->focused_view->mapped)
        bjson_str(b, view_app_id(s->focused_view));
    else
        bputs(b, "null");

    bputs(b, ",\"active\":");
    if (s->active_constraint) json_constraint(b, s, s->active_constraint);
    else                      bputs(b, "null");
    bprintf(b, ",\"bound\":%s",
            (s->active_constraint &&
             pfocus == s->active_constraint->surface) ? "true" : "false");

    bputs(b, ",\"constraints\":[");
    if (s->pointer_constraints) {
        struct wlr_pointer_constraint_v1 *c;
        int first = 1;
        wl_list_for_each(c, &s->pointer_constraints->constraints, link) {
            if (!first) bputs(b, ",");
            first = 0;
            json_constraint(b, s, c);
        }
    }
    bputs(b, "]");

    /* Game mode's own confinement. `box` is what actually holds the cursor;
     * `content` is the game's picture and `output` its screen, reported apart
     * because the gap between them IS the letterbox bar. */
    syn_view_t *gv = game_probe_view(s);
    bprintf(b, ",\"game\":{\"mode\":%s,\"active\":%s,\"confine\":%s",
            s->config.game_mode ? "true" : "false",
            s->game.active      ? "true" : "false",
            s->config.game_confine_pointer ? "true" : "false");
    bputs(b, ",\"view\":");
    if (gv) bjson_str(b, view_app_id(gv)); else bputs(b, "null");
    bprintf(b, ",\"focused\":%s", (gv && s->focused_view == gv) ? "true" : "false");

    struct wlr_box bx;
    if (gv && gv->output) {
        output_box_of(s, gv->output, &bx);
        bprintf(b, ",\"output\":[%d,%d,%d,%d]", bx.x, bx.y, bx.width, bx.height);
    } else {
        bputs(b, ",\"output\":null");
    }
    if (gv && view_scaled_content_box(gv, &bx))
        bprintf(b, ",\"content\":[%d,%d,%d,%d]", bx.x, bx.y, bx.width, bx.height);
    else
        bputs(b, ",\"content\":null");
    if (game_pointer_box(s, &bx))
        bprintf(b, ",\"box\":[%d,%d,%d,%d]", bx.x, bx.y, bx.width, bx.height);
    else
        bputs(b, ",\"box\":null");
    bputs(b, "}}\n");
}

/*
 * The applications this desktop has opened, newest first.
 *
 * ⚠ RESOLVED HERE, not stored resolved. recent.c keeps app_ids and nothing
 * else; this is where each one becomes something drawable, through the same
 * icon_lookup() the dock and the app grid use — so there is one answer on this
 * desktop to "what is this application called and how is it started", and a
 * caller of this command gets the same one they do.
 *
 * ⚠ AN UNRESOLVED ID IS STILL RETURNED, with `known` false. icon_lookup falls
 * back to the app_id for both the name and the command, which is right for the
 * dock (a running window it can label roughly) and WRONG for a launcher: an
 * app_id is not a command, and running one would be a guess with a shell behind
 * it. The flag is how a caller tells the difference; big screen mode draws only
 * the ones it can actually start.
 *
 * `icon` is the .desktop Icon= value — a THEME NAME, not a path. Resolving it
 * belongs to whatever is drawing, which already has an icon theme loaded and a
 * size it wants; a path chosen here would be one guess at both.
 */
static void cmd_recent(syn_server_t *s, ipc_buf_t *b)
{
    (void)s;
    char ids[RECENT_KEEP_MAX][RECENT_ID_MAX];
    int n = recent_apps_load(ids, RECENT_KEEP_MAX);

    bputs(b, "[");
    for (int i = 0; i < n; i++) {
        const syn_icon_entry_t *e = icon_lookup(ids[i]);
        /* ⚠ e->resolved, NOT a comparison of exec against the app_id. That
         * was the first version of this line and it is wrong for every
         * application whose Exec is its own name — which is most of the ones
         * on this desktop: syntty's .desktop resolved perfectly, gave a proper
         * Name= of "Terminal", and was still reported unknown because
         * `Exec=syntty` happens to equal the app_id. */
        bool known = e && e->resolved;

        if (i) bputs(b, ",");
        bputs(b, "{\"app_id\":");
        bjson_str(b, ids[i]);
        bputs(b, ",\"name\":");
        bjson_str(b, e ? e->display_name : ids[i]);
        bputs(b, ",\"exec\":");
        bjson_str(b, e ? e->exec : "");
        bputs(b, ",\"icon\":");
        bjson_str(b, e ? e->icon_hint : "");
        bprintf(b, ",\"known\":%s}", known ? "true" : "false");
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

/*
 * The BIND TABLE, as the desktop actually holds it right now.
 *
 * ⚠ THIS EXISTS BECAUSE EVERY OTHER LIST OF THE KEYS IS A COPY. render.c's
 * welcome menu carried a hand-typed chord per row and said so at length — the
 * command bar has been on Super+Space, on Super+= and back on Super+Space, and
 * each move left that column naming the old one. The welcome guide is QML now
 * and lives outside the compositor, so it cannot read config.binds directly;
 * without this command its only option would be to type the strings out again
 * and inherit the same drift, one process further away.
 *
 * The chord is rendered HERE, by ctlpanel_combo_str(), so the guide, the
 * shortcuts palette and the control panel all print "Super+Shift+C" the same
 * way. A client that reassembled it from a modifier mask would be the second
 * spelling this is meant to prevent.
 *
 * `arg` is reported rather than folded in: an action can legitimately be bound
 * several times (`control` bare and `control audio` are two rows of the control
 * panel's world) and only the caller knows which one it means. The bare bind is
 * the one a menu row runs, and a caller that wants it takes the first entry
 * with an empty arg — the rule welcome_hint() applied inside the compositor.
 */
static void cmd_binds(syn_server_t *s, ipc_buf_t *b)
{
    bputs(b, "[");
    for (int i = 0; i < s->config.bind_count; i++) {
        const syn_bind_t *bd = &s->config.binds[i];
        char combo[64];
        ctlpanel_combo_str(bd->mods, bd->sym, combo, sizeof(combo));
        if (i) bputs(b, ",");
        bputs(b, "{\"action\":");
        bjson_str(b, bd->action);
        bputs(b, ",\"arg\":");
        bjson_str(b, bd->arg);
        bputs(b, ",\"key\":");
        bjson_str(b, combo);
        bputs(b, "}");
    }
    bputs(b, "]\n");
}

/* `synctl hdr` — what the hardware will actually accept.
 *
 * ⛔ NOTHING IS COMMITTED. hdrprobe_report() asks with wlr_output_test_state(),
 * which the atomic backend validates against the kernel with TEST_ONLY. Asking
 * by committing would put the display into HDR to find out whether it can be —
 * which on half-supporting hardware is a black screen on somebody's only
 * monitor, from a query. */
static void hdr_emit(void *ctx, const char *line)
{
    ipc_buf_t *b = ctx;
    if (b->len && b->buf[b->len - 1] != '[') bputs(b, ",");
    bjson_str(b, line);
}

static void cmd_hdr(syn_server_t *s, ipc_buf_t *b)
{
    bputs(b, "[");
    hdrprobe_report(s, hdr_emit, b);
    bputs(b, "]");
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

        /* ⚠ ANSWER HONESTLY. This was an unconditional {"ok":true}, so a typo'd
         * action — or one renamed out from under a script — reported success
         * while doing nothing at all. The compositor logs `unknown bind action`
         * either way, but a caller reading this socket is not reading the
         * journal, and "ok" is the one answer it cannot act on.
         *
         * Still no report of whether the action WORKED: most of them spawn and
         * return, so there is nothing truthful to say. Matched, not succeeded. */
        if (!synui_binding_execute(s, rest, arg)) {
            /* bjson_str, not bputs: `rest` is whatever came down the socket,
             * and a quote in it would otherwise break the document open. */
            bputs(out, "{\"error\":\"unknown action\",\"action\":");
            bjson_str(out, rest);
            bputs(out, "}\n");
            return;
        }
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

    if (strcmp(line, "recent") == 0) {
        cmd_recent(s, out);
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
    if (strcmp(line, "hdr") == 0) {
        cmd_hdr(s, out);
        return;
    }
    if (strcmp(line, "binds") == 0 || strcmp(line, "keys") == 0) {
        cmd_binds(s, out);
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
    /* Where the pointer is, in layout coordinates.
     *
     * Answers from s->cursor rather than from the cached cursor_x/cursor_y,
     * because those are only written by the motion path: a cursor warped by a
     * layout change or by a client's constraint would be reported at a stale
     * position by the cache and at its real one by wlr_cursor. */
    if (strcmp(line, "cursor") == 0) {
        bprintf(out, "{\"x\":%.3f,\"y\":%.3f}\n", s->cursor->x, s->cursor->y);
        return;
    }
    if (strcmp(line, "pointer") == 0) {
        cmd_pointer(s, out);
        return;
    }
    /*
     * The keyboard layouts, and how to move between them.
     *
     *   synctl layout            — list them, and say which one is typing
     *   synctl layout next|prev  — walk them
     *   synctl layout <name|N>   — go to one by name or index
     *
     * The lock screen's chip is the GUI half of exactly this (Super+Space, or
     * clicking it); a desktop with two layouts needs the same verb without
     * locking the screen first, and a script needs it at all.
     */
    if (strncmp(line, "layout", 6) == 0 && (line[6] == ' ' || line[6] == '\0')) {
        const char *arg = line[6] ? line + 7 : "";
        while (*arg == ' ') arg++;

        if (*arg) {
            if (strcmp(arg, "next") == 0) {
                kbd_layout_cycle(s, +1);
            } else if (strcmp(arg, "prev") == 0) {
                kbd_layout_cycle(s, -1);
            } else {
                int idx = kbd_layout_from_name(s, arg);
                if (idx < 0) {
                    bputs(out, "{\"error\":\"no such layout\",\"layout\":");
                    bjson_str(out, arg);
                    bputs(out, "}\n");
                    return;
                }
                kbd_layout_set(s, idx);
            }
            /* The lock screen's chip is drawn, not bound — nothing repaints it
             * on its own, and a layout changed from a script while the screen
             * is locked is precisely when the chip must be right. */
            if (s->nlock.active) lock_render(s);
        }

        int n = kbd_layout_count(s), act = kbd_layout_active(s);
        bprintf(out, "{\"active\":%d,\"layouts\":[", act);
        for (int i = 0; i < n; i++) {
            char lab[64];
            kbd_layout_label(s, i, lab, sizeof(lab));
            if (i) bputs(out, ",");
            bjson_str(out, lab);
        }
        bputs(out, "]}\n");
        return;
    }
    /*
     * The weather.
     *
     *   synctl weather              — the reading, or {"on":false}
     *   synctl weather on|off       — the NETWORK switch, for every surface
     *   synctl weather refresh      — ask now instead of at the next tick
     *
     * ⚠ ONE SWITCH, THREE SURFACES. The lock screen, the bar module and the
     * desktop widget all draw the same reading from the same fetch; `off` is
     * the machine not talking to Open-Meteo at all, which is the only part of
     * any of this that leaves the box. Whether a given surface SHOWS what has
     * been fetched is that surface's own furniture switch — the bar's
     * right-click menu, `synui-widgets weather`, the Super+Z row — exactly the
     * split Updates.qml documents between its timer and its module.
     */
    if (strncmp(line, "weather", 7) == 0 && (line[7] == ' ' || line[7] == '\0')) {
        const char *arg = line[7] ? line + 8 : "";
        while (*arg == ' ') arg++;

        if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
            bool on = arg[1] == 'n';
            if (on != (bool)s->config.weather) {
                s->config.weather = on;
                weather_enabled_changed(s);
                saver_state_save(s);      /* the Super+Z row's file: one owner */
            }
        } else if (strcmp(arg, "refresh") == 0) {
            if (!s->config.weather) {
                bputs(out, "{\"error\":\"weather is off\"}\n");
                return;
            }
            weather_refresh(s, true);
        } else if (*arg) {
            bputs(out, "{\"error\":\"usage: weather [on|off|refresh]\"}\n");
            return;
        }

        syn_weather_now_t w;
        if (!s->config.weather || !weather_current(&w)) {
            /* "on but nothing yet" and "off" are different answers: the first
             * is a machine that has been asked and has not been told, which is
             * every machine for the first seconds after it is switched on. */
            bprintf(out, "{\"on\":%s,\"have\":false}\n",
                    s->config.weather ? "true" : "false");
            return;
        }
        bprintf(out, "{\"on\":true,\"have\":true,\"temp\":%d,\"unit\":\"%c\","
                     "\"age\":%ld,\"stale\":%s,\"cond\":",
                w.temp, w.unit, (long)w.age, w.stale ? "true" : "false");
        bjson_str(out, w.cond);
        bputs(out, ",\"place\":");
        bjson_str(out, w.place);
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
                   "\"activeworkspace\",\"activewindow\",\"cursor\",\"pointer\","
                   "\"recent\",\"binds\",\"version\","
                   "\"layout [next|prev|<name>]\","
                   "\"weather [on|off|refresh]\","
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
