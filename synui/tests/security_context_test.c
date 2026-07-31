/*
 * security_context_test.c — does a sandboxed client actually lose the
 * privileged globals?
 *
 * synui advertises screen capture, input interception, input injection and
 * clipboard snooping to every client, which is the wlroots default. The global
 * filter in synui_main.c withholds those from any client holding a
 * security-context. The smoke suite already proves the permissive half — grim
 * still captures — but that only shows nothing BROKE. This is the other half:
 * that the restriction actually restricts.
 *
 * Both halves matter, and the restricting one is the one that fails silently.
 * A filter that never matches looks exactly like a filter that works, right up
 * until someone relies on it.
 *
 * Method, in one process, no fork:
 *
 *   1. Connect normally and enumerate the globals. Assert the privileged ones
 *      ARE present — otherwise a later absence proves nothing, it could just
 *      mean the compositor never offered them.
 *   2. Bind wp_security_context_manager_v1 and create_listener() on a real
 *      bound+listening AF_UNIX socket of our own, with a pipe read end as
 *      close_fd. Set a sandbox engine/app id and commit.
 *   3. Connect a SECOND wl_display through that socket. The compositor accepts
 *      it there and tags it with the context.
 *   4. Enumerate again. Assert every privileged global is GONE, and that
 *      ordinary ones (wl_compositor, wl_seat, xdg_wm_base) survive — a filter
 *      that hid everything would be a different bug, not a fix.
 *
 * Needs a running compositor on $WAYLAND_DISPLAY; smoke.sh runs it against the
 * nested instance it has already started.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-client.h>

#include "security-context-v1-client-protocol.h"

static int failures;

static void ok(const char *name, int cond)
{
    printf("  %s - %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* Globals the filter must withhold from a sandboxed client. Mirrors
 * privileged_globals[] in src/synui_main.c — keep the two in step. */
static const char *const privileged[] = {
    "zwlr_screencopy_manager_v1",
    "zwlr_export_dmabuf_manager_v1",
    "zwlr_data_control_manager_v1",
    "ext_data_control_manager_v1",
    "zwp_virtual_keyboard_manager_v1",
    "zwp_input_method_manager_v2",
    "zwlr_foreign_toplevel_manager_v1",
    "ext_foreign_toplevel_list_v1",
    "zwlr_gamma_control_manager_v1",
    "zwlr_output_power_manager_v1",
    "zwlr_output_manager_v1",
    "wp_security_context_manager_v1",
};
#define N_PRIV (sizeof(privileged) / sizeof(privileged[0]))

/* Ordinary globals that must survive — proves the filter is selective. */
static const char *const ordinary[] = {
    "wl_compositor", "wl_seat", "wl_shm", "xdg_wm_base",
};
#define N_ORD (sizeof(ordinary) / sizeof(ordinary[0]))

struct scan {
    bool  priv_seen[N_PRIV];
    bool  ord_seen[N_ORD];
    struct wp_security_context_manager_v1 *sec_mgr;
    uint32_t sec_mgr_name;
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t version)
{
    struct scan *s = data;

    for (size_t i = 0; i < N_PRIV; i++)
        if (strcmp(iface, privileged[i]) == 0) s->priv_seen[i] = true;
    for (size_t i = 0; i < N_ORD; i++)
        if (strcmp(iface, ordinary[i]) == 0) s->ord_seen[i] = true;

    if (strcmp(iface, wp_security_context_manager_v1_interface.name) == 0) {
        s->sec_mgr_name = name;
        s->sec_mgr = wl_registry_bind(reg, name,
                                      &wp_security_context_manager_v1_interface,
                                      version < 1 ? version : 1);
    }
    (void)version;
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name)
{ (void)data; (void)reg; (void)name; }

static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

/* Enumerate a display's globals into `out`. */
static void scan_display(struct wl_display *d, struct scan *out)
{
    memset(out, 0, sizeof(*out));
    struct wl_registry *reg = wl_display_get_registry(d);
    wl_registry_add_listener(reg, &reg_listener, out);
    /* Twice: the first settles the registry, the second any binds it caused. */
    wl_display_roundtrip(d);
    wl_display_roundtrip(d);
    wl_registry_destroy(reg);
}

int main(void)
{
    printf("security-context: are privileged globals withheld from a sandbox?\n");

    struct wl_display *plain = wl_display_connect(NULL);
    if (!plain) {
        fprintf(stderr, "  cannot connect to $WAYLAND_DISPLAY: %s\n",
                strerror(errno));
        return 2;
    }

    /* ── 1. Unsandboxed baseline ───────────────────────────── */
    struct scan base;
    scan_display(plain, &base);

    int base_priv = 0;
    for (size_t i = 0; i < N_PRIV; i++) if (base.priv_seen[i]) base_priv++;
    printf("  (unsandboxed sees %d/%zu privileged globals)\n",
           base_priv, N_PRIV);

    /* If the compositor never offered these, the test below is vacuous. */
    ok("unsandboxed client sees the privileged globals", base_priv > 0);
    ok("security-context manager is advertised", base.sec_mgr != NULL);
    if (!base.sec_mgr) {
        fprintf(stderr, "  no wp_security_context_manager_v1 — "
                        "synui did not create it\n");
        return 1;
    }

    /* ── 2. Build a sandbox socket and commit a context ─────── */
    char sock_path[] = "/tmp/synui-secctx-test-XXXXXX";
    int tmpfd = mkstemp(sock_path);
    if (tmpfd < 0) { perror("mkstemp"); return 2; }
    close(tmpfd);
    unlink(sock_path);            /* need the NAME free for bind() */

    int lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) { perror("socket"); return 2; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 2;
    }
    if (listen(lfd, 4) < 0) { perror("listen"); return 2; }

    /* close_fd: the compositor revokes the context when this becomes
     * readable/EOF, so we hold the write end open for the test's lifetime. */
    int closefd[2];
    if (pipe(closefd) < 0) { perror("pipe"); return 2; }

    struct wp_security_context_v1 *ctx =
        wp_security_context_manager_v1_create_listener(base.sec_mgr,
                                                       lfd, closefd[0]);
    ok("create_listener returned a context", ctx != NULL);
    if (!ctx) return 1;

    wp_security_context_v1_set_sandbox_engine(ctx, "synui.test");
    wp_security_context_v1_set_app_id(ctx, "org.synapseos.SecCtxTest");
    wp_security_context_v1_set_instance_id(ctx, "1");
    wp_security_context_v1_commit(ctx);
    wl_display_roundtrip(plain);

    /* The compositor owns its copies now. */
    close(lfd);
    close(closefd[0]);

    /* ── 3. Connect THROUGH the sandbox socket ──────────────── */
    int cfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (cfd < 0) { perror("socket"); return 2; }
    if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect to sandbox socket"); return 2;
    }

    struct wl_display *sandboxed = wl_display_connect_to_fd(cfd);
    ok("sandboxed client connected", sandboxed != NULL);
    if (!sandboxed) { unlink(sock_path); return 1; }

    /* ── 4. The actual assertion ────────────────────────────── */
    struct scan sb;
    scan_display(sandboxed, &sb);

    for (size_t i = 0; i < N_PRIV; i++) {
        /* Only meaningful for globals the unsandboxed client could see. */
        if (!base.priv_seen[i]) continue;
        char msg[160];
        snprintf(msg, sizeof(msg), "sandbox is DENIED %s", privileged[i]);
        ok(msg, !sb.priv_seen[i]);
    }

    for (size_t i = 0; i < N_ORD; i++) {
        if (!base.ord_seen[i]) continue;
        char msg[160];
        snprintf(msg, sizeof(msg), "sandbox still gets %s", ordinary[i]);
        ok(msg, sb.ord_seen[i]);
    }

    /* Destroy the proxies before dropping the displays. wl_display_disconnect()
     * releases the object map but not the wl_proxy allocations themselves —
     * those belong to whoever bound them — so leaving these two behind cost
     * 192 bytes and aborted the whole test under -Db_sanitize=address. smoke.sh
     * reads any non-zero exit here as "a sandboxed client was NOT denied the
     * privileged globals", so a leak in the test reported itself as a security
     * regression. sb.sec_mgr is NULL whenever the filter is doing its job; it
     * is destroyed anyway so that a regression fails on the assertion above
     * rather than on a leak. */
    if (sb.sec_mgr) wp_security_context_manager_v1_destroy(sb.sec_mgr);
    wl_display_disconnect(sandboxed);
    close(closefd[1]);
    wp_security_context_v1_destroy(ctx);
    wp_security_context_manager_v1_destroy(base.sec_mgr);
    wl_display_disconnect(plain);
    unlink(sock_path);

    printf(failures ? "security-context: FAILED (%d)\n"
                    : "security-context: all passed\n", failures);
    return failures ? 1 : 0;
}
