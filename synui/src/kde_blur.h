/*
 * kde_blur.h — org_kde_kwin_blur ("blur behind") for synui.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_KDE_BLUR_H
#define SYNUI_KDE_BLUR_H

#include <stdbool.h>
#include "synui.h"

struct wlr_surface;

/*
 * Advertise org_kde_kwin_blur_manager. Returns false only if the global could
 * not be created; synui runs fine without it (clients just stay opaque).
 */
bool syn_kde_blur_init(syn_server_t *s);

/*
 * True when this surface has asked for blur behind it and committed that
 * request. anim.c consults it per scene buffer.
 */
bool syn_kde_blur_wants(struct wlr_surface *surface);

#endif /* SYNUI_KDE_BLUR_H */
