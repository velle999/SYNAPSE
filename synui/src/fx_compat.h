/*
 * fx_compat.h — minimal hand-declarations for the one scenefx render-pass entry
 * point synui uses (effects.c / matrix.c).
 *
 * We cannot include scenefx's own <scenefx/render/pass.h>: it transitively
 * `#include "render/egl.h"`, a wlroots-PRIVATE header that is not installed, so
 * the include fails for any external consumer of the packaged library. SwayFX
 * only gets away with it because it builds scenefx as a subproject with the
 * wlroots source tree on the include path.
 *
 * All we need is fx_renderer_begin_buffer_pass, purely to make the fx_renderer's
 * (otherwise unreachable) EGL context current long enough to capture it. The
 * returned struct fx_gles_render_pass embeds a `struct wlr_render_pass base` as
 * its FIRST member, so the returned pointer is passed straight to
 * wlr_render_pass_submit() (the same first-member-cast idiom wlroots uses).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_FX_COMPAT_H
#define SYNUI_FX_COMPAT_H

#include <wlr/render/pass.h>   /* wlr_buffer_pass_options, wlr_render_pass_submit */

struct wlr_swapchain;
struct fx_gles_render_pass;

/* Mirror of scenefx's fx_buffer_pass_options (scenefx/render/pass.h). */
struct fx_buffer_pass_options {
	const struct wlr_buffer_pass_options *base;
	struct wlr_swapchain *swapchain;
};

struct fx_gles_render_pass *fx_renderer_begin_buffer_pass(
	struct wlr_renderer *renderer, struct wlr_buffer *buffer,
	struct wlr_output *output, const struct fx_buffer_pass_options *options);

#endif /* SYNUI_FX_COMPAT_H */
