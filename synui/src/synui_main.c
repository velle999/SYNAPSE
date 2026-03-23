/*
 * synui_main.c — SynapseOS Wayland Compositor
 *
 * Entry point and wlroots initialization.
 *
 * wlroots gives us:
 *   - Backend abstraction (DRM/KMS, Wayland, X11 nested, headless)
 *   - Scene graph for compositing
 *   - XDG shell surface management
 *   - Input (libinput via wlroots)
 *   - Output management
 *
 * We add on top:
 *   - AI layout engine (synapd IPC)
 *   - Neural overlay (rendered each frame)
 *   - Command bar (Super+Space)
 *   - Security borders (synguard event feed)
 *   - Workspace intents
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>
#include <sys/syscall.h>

#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation.h>

#include "synui.h"

/* ── Output events ───────────────────────────────────────── */
static void output_frame(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    /* Update overlay state each frame if visible */
    if (output->server->overlay.visible)
        overlay_update(output->server);

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data)
{
    syn_server_t *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Configure output state */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    syn_output_t *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    wlr_output->data = output;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       output->scene_output);

    wl_list_insert(&server->outputs, &output->link);

    wlr_log(WLR_INFO, "synui: new output %s %dx%d",
            wlr_output->name, wlr_output->width, wlr_output->height);

    /* Re-apply layout for current workspace */
    layout_apply(server, &server->workspaces[server->active_workspace]);
}

/* ── XDG surface events ──────────────────────────────────── */
static void xdg_surface_map(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, map);
    view->mapped = 1;
    focus_view(view->server, view, view->xdg_surface->surface);
    layout_apply(view->server, view->workspace);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, unmap);
    view->mapped = 0;
    /* Remove borders from scene */
    if (view->border_top)    { wlr_scene_node_destroy(&view->border_top->node);    view->border_top    = NULL; }
    if (view->border_bottom) { wlr_scene_node_destroy(&view->border_bottom->node); view->border_bottom = NULL; }
    if (view->border_left)   { wlr_scene_node_destroy(&view->border_left->node);   view->border_left   = NULL; }
    if (view->border_right)  { wlr_scene_node_destroy(&view->border_right->node);  view->border_right  = NULL; }
}

static void xdg_surface_destroy(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->link);
    free(view);
}

static void xdg_surface_commit(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, commit);
    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        /* Update borders when surface geometry changes */
        if (view->mapped)
            view_update_borders(view);
    }
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, request_maximize);
    view->maximized = !view->maximized;
    wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, view->maximized);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, request_fullscreen);
    view->fullscreen = !view->fullscreen;
    wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, view->fullscreen);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data)
{
    syn_server_t *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        struct wlr_xdg_surface *parent =
            wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
        assert(parent);
        struct wlr_scene_tree *parent_tree = parent->data;
        xdg_surface->data = wlr_scene_xdg_surface_create(parent_tree, xdg_surface);
        return;
    }
    assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

    syn_view_t *view = calloc(1, sizeof(*view));
    view->xdg_surface = xdg_surface;
    view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
    view->scene_tree->node.data = view;
    xdg_surface->data = view->scene_tree;

    /* Assign to active workspace */
    view->workspace = &server->workspaces[server->active_workspace];
    wl_list_insert(&view->workspace->windows, &view->link);

    view->map.notify = xdg_surface_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);

    view->unmap.notify = xdg_surface_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

    view->destroy.notify = xdg_surface_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

    view->commit.notify = xdg_surface_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);

    view->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_surface->toplevel->events.request_maximize,
                  &view->request_maximize);

    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen,
                  &view->request_fullscreen);

    /* Assign server pointer so view callbacks can reach it */
    view->server = server;

    /* Check if process has AI_CTX set */
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(xdg_surface->resource),
                              &pid, NULL, NULL);
    if (pid > 0) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            char comm[32] = {0};
            fread(comm, 1, sizeof(comm)-1, f);
            fclose(f);
            /* Comm has newline — strip it */
            char *nl = strchr(comm, '\n');
            if (nl) *nl = '\0';

            /* Announce new window to AI for layout suggestion */
            char prompt[256];
            snprintf(prompt, sizeof(prompt),
                     "[WINDOW_OPENED] app=%s pid=%d workspace=%s — "
                     "suggest layout adjustment? Reply YES or NO only.",
                     comm, pid,
                     server->workspaces[server->active_workspace].name);

            syn_ai_request_t req = {
                .type = AI_MSG_QUERY_LAYOUT,
                .id   = (uint64_t)pid,
            };
            strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
            ai_thread_send(server, &req);
        }
    }

    wlr_log(WLR_DEBUG, "synui: new toplevel");
}

/* ── Server init ─────────────────────────────────────────── */
int synui_init(syn_server_t *s)
{
    /* Tell synui it's a SynapseOS compositor */
    setenv("SYNUI_RUNNING", "1", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "SynapseOS", 1);
    /* WAYLAND_DISPLAY will be set after socket creation below */

    /* Declare AI intent to the kernel */
    struct {
        uint32_t flags;
        char intent[256];
        uint32_t priority_hint;
        uint32_t reserved[4];
    } ctx_args = {
        .flags = (1 << 5) | (1 << 2),  /* INTERACTIVE | LATENCY */
        .priority_hint = 90,
    };
    strncpy(ctx_args.intent,
            "Wayland compositor — I manage all window rendering and user input",
            sizeof(ctx_args.intent) - 1);
    syscall(NR_AI_CTX_SET, &ctx_args);

    /* Create Wayland display */
    s->display = wl_display_create();
    if (!s->display) {
        fprintf(stderr, "synui: wl_display_create() failed\n");
        return -1;
    }

    /* Create wlroots backend */
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), NULL);
    if (!s->backend) {
        fprintf(stderr, "synui: wlr_backend_autocreate() failed (WLR_BACKENDS=%s WLR_RENDERER=%s)\n",
                getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)",
                getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");
        wlr_log(WLR_ERROR, "synui: failed to create backend");
        return -1;
    }

    s->renderer = wlr_renderer_autocreate(s->backend);
    if (!s->renderer) {
        fprintf(stderr, "synui: wlr_renderer_autocreate() failed (WLR_RENDERER=%s)\n",
                getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");
        return -1;
    }
    wlr_renderer_init_wl_display(s->renderer, s->display);

    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    if (!s->allocator) {
        fprintf(stderr, "synui: wlr_allocator_autocreate() failed\n");
        return -1;
    }

    /* Compositor protocols */
    s->compositor = wlr_compositor_create(s->display, 5, s->renderer);
    wlr_subcompositor_create(s->display);
    wlr_data_device_manager_create(s->display);
    wlr_viewporter_create(s->display);
    wlr_presentation_create(s->display);

    /* Output layout */
    s->output_layout = wlr_output_layout_create(s->display);

    /* Scene graph */
    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    /* Background: dark rect so the compositor isn't pure black */
    float bg_color[4] = { 0.07f, 0.07f, 0.12f, 1.0f };
    s->bg_rect = wlr_scene_rect_create(&s->scene->tree, 8192, 8192, bg_color);
    wlr_scene_node_set_position(&s->bg_rect->node, -4096, -4096);
    wlr_scene_node_lower_to_bottom(&s->bg_rect->node);

    /* XDG shell */
    s->xdg_shell = wlr_xdg_shell_create(s->display, 3);

    /* Layer shell */
    s->layer_shell = wlr_layer_shell_v1_create(s->display, 4);

    /* Seat */
    s->seat = wlr_seat_create(s->display, "seat0");

    /* Cursor */
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);

    /* Initialize workspaces */
    const char *ws_names[WORKSPACE_MAX] = {
        "main", "web", "code", "terminal", "media",
        "docs", "chat", "sys", "scratch"
    };
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        s->workspaces[i].index   = i;
        s->workspaces[i].layout  = LAYOUT_TILING;
        s->workspaces[i].visible = (i == 0);
        strncpy(s->workspaces[i].name, ws_names[i], WORKSPACE_NAME_LEN - 1);
        wl_list_init(&s->workspaces[i].windows);
    }
    s->active_workspace = 0;

    /* Wire up listeners */
    s->new_output.notify = server_new_output;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&s->xdg_shell->events.new_surface, &s->new_xdg_surface);

    wl_list_init(&s->outputs);
    wl_list_init(&s->keyboards);

    /* Input */
    input_setup(s);

    /* Add the socket */
    const char *socket = wl_display_add_socket_auto(s->display);
    if (!socket) {
        wlr_log(WLR_ERROR, "synui: failed to create Wayland socket");
        return -1;
    }
    setenv("WAYLAND_DISPLAY", socket, 1);
    wlr_log(WLR_INFO, "synui: running on WAYLAND_DISPLAY=%s", socket);

    /* Write socket name for synui-foot.service */
    FILE *sf = fopen("/tmp/synui-display", "w");
    if (sf) { fprintf(sf, "%s\n", socket); fclose(sf); }

    /* Start AI thread */
    ai_thread_start(s);

    /* Connect to synapd */
    synui_synapd_connect(s);

    return 0;
}

int synui_run(syn_server_t *s)
{
    if (!wlr_backend_start(s->backend)) {
        fprintf(stderr, "synui: wlr_backend_start() failed — check DRM/KMS access and kernel logs\n");
        wlr_log(WLR_ERROR, "synui: failed to start backend");
        return -1;
    }
    wl_display_run(s->display);
    return 0;
}

void synui_destroy(syn_server_t *s)
{
    wl_display_destroy_clients(s->display);
    wlr_scene_node_destroy(&s->scene->tree.node);
    wlr_xcursor_manager_destroy(s->cursor_mgr);
    wlr_cursor_destroy(s->cursor);
    wlr_output_layout_destroy(s->output_layout);
    wlr_backend_destroy(s->backend);
    wl_display_destroy(s->display);
}

/* ── VM detection ────────────────────────────────────────── */
/*
 * Reads /sys/class/dmi/id/sys_vendor to detect hypervisors.
 * Returns 1 if running in VirtualBox, VMware, or QEMU; 0 otherwise.
 */
static int detect_vm(void)
{
    static const char *const vendors[] = {
        "VirtualBox", "VMware", "QEMU", "innotek", "KVM", "Xen", NULL
    };
    FILE *f = fopen("/sys/class/dmi/id/sys_vendor", "r");
    if (!f) return 0;
    char buf[128] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    for (int i = 0; vendors[i]; i++) {
        if (strstr(buf, vendors[i]))
            return 1;
    }
    return 0;
}

/* ── Entry point ─────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS Wayland Compositor\n"
        "\n"
        "Options:\n"
        "  --no-ai        Disable AI features (layout hints, command bar AI)\n"
        "  --overlay      Start with neural overlay visible\n"
        "  -d, --debug    Enable verbose wlroots logging\n"
        "  -v, --version  Print version\n"
        "  -h, --help     This help\n"
        "\n"
        "Keybindings:\n"
        "  Super+Enter        Open terminal\n"
        "  Super+Space        Open AI command bar\n"
        "  Super+A            Toggle neural overlay\n"
        "  Super+1..9         Switch workspace\n"
        "  Super+Shift+1..9   Move window to workspace\n"
        "  Super+L            Next layout mode\n"
        "  Super+Q            Close focused window\n"
        "  Super+Shift+Q      Quit compositor\n",
        prog
    );
}

int main(int argc, char *argv[])
{
    int debug = 0;
    int no_ai = 0;
    int start_overlay = 0;

    static struct option long_opts[] = {
        {"no-ai",   no_argument, 0, 'N'},
        {"overlay", no_argument, 0, 'O'},
        {"debug",   no_argument, 0, 'd'},
        {"version", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "NOdvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'N': no_ai = 1; break;
        case 'O': start_overlay = 1; break;
        case 'd': debug = 1; break;
        case 'v':
            printf("synui %s (SynapseOS Wayland Compositor)\n", SYNUI_VERSION);
            return 0;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);

    /* Detect VM and force software rendering before any wlroots init */
    if (detect_vm()) {
        fprintf(stderr, "synui: VM/hypervisor detected — forcing pixman renderer\n");
        setenv("WLR_RENDERER", "pixman", 1);
        /* Only set WLR_BACKENDS if caller hasn't already chosen one */
        if (!getenv("WLR_BACKENDS"))
            setenv("WLR_BACKENDS", "drm,libinput", 1);
        /* Disable hardware cursor; vmwgfx can't do it */
        setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
    }

    syn_server_t server = {0};
    if (no_ai) {
        atomic_store(&server.ai_connected, 0);
    }
    if (start_overlay) {
        server.overlay.visible = 1;
    }

    fprintf(stderr, "synui: starting (WLR_RENDERER=%s WLR_BACKENDS=%s)\n",
            getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)",
            getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)");

    if (synui_init(&server) < 0) {
        fprintf(stderr, "synui: initialization failed\n");
        return 1;
    }

    fprintf(stderr, "synui %s — SynapseOS compositor running\n", SYNUI_VERSION);

    int ret = synui_run(&server);
    synui_destroy(&server);
    return ret;
}
