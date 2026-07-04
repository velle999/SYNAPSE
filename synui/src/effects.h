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

/* Animation triggers (no-ops when the pass is unavailable/disabled):
 * focus change ramps chromatic aberration (L3); a window close fires a
 * brief screen glitch (L2 interim). A synguard ALERT/DENY verdict on any
 * mapped window sustains the glitch for as long as it stands (L4) — that
 * one is detected per-frame, no trigger needed. */
void effects_notify_focus(syn_server_t *s);
void effects_notify_close(syn_server_t *s);

#endif
