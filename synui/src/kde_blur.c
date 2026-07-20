/*
 * kde_blur.c — org_kde_kwin_blur ("blur behind") for synui.
 *
 * KWin's protocol for "blur whatever is behind my translucent surface". synui
 * implements it for a reason that has little to do with the blur itself: many
 * Qt/Breeze clients probe for this global and only paint a translucent
 * background *if* it is advertised, falling back to a solid one otherwise so
 * their text stays readable on an unknown compositor. Dolphin, Kate, Konsole
 * and the Plasma applets all behave this way. So binding the manager is what
 * flips a large part of the KDE stack into its glass look at all; honouring the
 * request afterwards is what stops that look from being unreadable.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>

#include "kde-blur-protocol.h"
#include "kde_blur.h"

#define SYN_KDE_BLUR_VERSION 1

/*
 * Per-surface blur state, hung off wlr_surface::addons so any code holding a
 * surface can ask about it without synui keeping a side table that would then
 * have to track surface lifetimes itself. The addon's destroy hook fires from
 * wlr_addon_set_finish() when the surface goes away, which is what makes a
 * client that exits without releasing its blur object safe.
 */
struct syn_kde_blur {
    struct wlr_addon    addon;
    struct wlr_surface *surface;
    syn_server_t       *server;
    /* The org_kde_kwin_blur resource, or NULL once the client released it and
     * only the surface addon is keeping this alive. */
    struct wl_resource *resource;
    /* The client has committed its blur request. Blur is NOT applied before
     * that: the protocol's create/set_region/commit sequence exists precisely
     * so a client can describe the blur before asking for it. */
    bool active;
};

/* Distinguishes our addon from every other addon on the surface. The address is
 * the identity — the value is never read. */
static const int blur_addon_owner;

static void blur_addon_destroy(struct wlr_addon *addon);

static const struct wlr_addon_interface blur_addon_impl = {
    .name    = "syn_kde_blur",
    .destroy = blur_addon_destroy,
};

static struct syn_kde_blur *blur_from_surface(struct wlr_surface *surface)
{
    if (!surface) return NULL;
    struct wlr_addon *addon =
        wlr_addon_find(&surface->addons, &blur_addon_owner, &blur_addon_impl);
    if (!addon) return NULL;
    struct syn_kde_blur *blur;
    return wl_container_of(addon, blur, addon);
}

/*
 * A window's blur is decided in anim_apply_alpha, which normally only runs on
 * map/focus/config changes — all of which a client's blur request can easily
 * arrive after (Qt commits its blur object during startup, and Plasma applets
 * toggle it at runtime). Without a nudge the request would sit unapplied until
 * the next unrelated focus change, which reads as "blur only works if I click
 * away and back". Re-walking every view is a handful of buffer writes and this
 * fires once or twice per window in its lifetime.
 */
static void blur_state_changed(syn_server_t *s)
{
    if (s) anim_apply_alpha_all(s);
}

static void blur_destroy(struct syn_kde_blur *blur)
{
    syn_server_t *s = blur->server;
    bool was_active = blur->active;

    if (blur->resource) {
        /* The surface died first. Leave the resource valid but inert rather
         * than destroying it under the client — it still owns that object and
         * may yet call release() on it. */
        wl_resource_set_user_data(blur->resource, NULL);
    }
    wlr_addon_finish(&blur->addon);
    free(blur);

    if (was_active) blur_state_changed(s);
}

static void blur_addon_destroy(struct wlr_addon *addon)
{
    struct syn_kde_blur *blur =
        wl_container_of(addon, blur, addon);
    blur_destroy(blur);
}

/* ---- org_kde_kwin_blur ------------------------------------------------ */

static void blur_handle_commit(struct wl_client *client,
                               struct wl_resource *resource)
{
    (void)client;
    struct syn_kde_blur *blur = wl_resource_get_user_data(resource);
    if (!blur || blur->active) return;
    blur->active = true;
    blur_state_changed(blur->server);
}

static void blur_handle_set_region(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
    /*
     * Accepted and deliberately ignored. scenefx's backdrop blur is a per-scene-
     * buffer flag, not a masked pass, so the unit synui can blur is the whole
     * surface — there is no way to honour a sub-region short of splitting the
     * buffer into scene nodes per rectangle, which would break input and damage
     * tracking for a refinement no client actually depends on. In practice every
     * client that sets a region sets either NULL or the full surface anyway:
     * the region exists in the protocol for panels with a cut-out, and those
     * degrade to "blurred everywhere" rather than to anything broken.
     *
     * Note this also means an *empty* region does not disable blur the way it
     * does under KWin. Reading a wl_region's rectangles needs wlroots-private
     * headers (0.19 stopped installing wlr_region.h), and clients turn blur off
     * with unset()/release(), not with an empty region.
     */
}

static void blur_handle_release(struct wl_client *client,
                                struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_blur_interface blur_impl = {
    .commit     = blur_handle_commit,
    .set_region = blur_handle_set_region,
    .release    = blur_handle_release,
};

static void blur_resource_destroy(struct wl_resource *resource)
{
    struct syn_kde_blur *blur = wl_resource_get_user_data(resource);
    if (!blur) return;   /* already torn down with the surface */
    blur->resource = NULL;
    blur_destroy(blur);
}

/* ---- org_kde_kwin_blur_manager ---------------------------------------- */

static void manager_handle_create(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *surface_resource)
{
    syn_server_t *s = wl_resource_get_user_data(resource);
    struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

    /* One blur object per surface. A client asking twice replaces the first,
     * matching KWin; the old object goes inert rather than fighting the new. */
    struct syn_kde_blur *old = blur_from_surface(surface);
    if (old) blur_destroy(old);

    struct wl_resource *blur_resource =
        wl_resource_create(client, &org_kde_kwin_blur_interface,
                           wl_resource_get_version(resource), id);
    if (!blur_resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct syn_kde_blur *blur = calloc(1, sizeof(*blur));
    if (!blur) {
        wl_resource_destroy(blur_resource);
        wl_client_post_no_memory(client);
        return;
    }

    blur->surface  = surface;
    blur->server   = s;
    blur->resource = blur_resource;
    wlr_addon_init(&blur->addon, &surface->addons, &blur_addon_owner,
                   &blur_addon_impl);

    wl_resource_set_implementation(blur_resource, &blur_impl, blur,
                                   blur_resource_destroy);
}

static void manager_handle_unset(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *surface_resource)
{
    (void)client; (void)resource;
    struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
    struct syn_kde_blur *blur = blur_from_surface(surface);
    if (blur) blur_destroy(blur);
}

static const struct org_kde_kwin_blur_manager_interface manager_impl = {
    .create = manager_handle_create,
    .unset  = manager_handle_unset,
};

static void manager_bind(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_blur_manager_interface,
                           version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

/* ---- public ----------------------------------------------------------- */

bool syn_kde_blur_init(syn_server_t *s)
{
    struct wl_global *global =
        wl_global_create(s->display, &org_kde_kwin_blur_manager_interface,
                         SYN_KDE_BLUR_VERSION, s, manager_bind);
    if (!global) {
        wlr_log(WLR_ERROR, "kde-blur: failed to create global");
        return false;
    }
    return true;
}

bool syn_kde_blur_wants(struct wlr_surface *surface)
{
    struct syn_kde_blur *blur = blur_from_surface(surface);
    return blur && blur->active;
}
