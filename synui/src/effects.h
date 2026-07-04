/*
 * effects.h — GLES2 post-process pass (CRT curvature, scanlines,
 * chromatic aberration) for synui.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_EFFECTS_H
#define SYNUI_EFFECTS_H

#include <stdbool.h>
#include "synui.h"

/* Compile the shader programs. Returns false (and leaves s->effects NULL)
 * when the renderer isn't GLES2 — pixman VMs take the plain scene path. */
bool effects_init(syn_server_t *s);
void effects_finish(syn_server_t *s);

/* Post-processed replacement for wlr_scene_output_commit(). Returns false
 * when the pass is unavailable/disabled or anything in it fails — the
 * caller must then fall back to the plain commit. */
bool effects_output_commit(syn_output_t *output);

/* Release the per-output offscreen swapchain (output destroy). */
void effects_output_destroy(syn_output_t *output);

#endif
