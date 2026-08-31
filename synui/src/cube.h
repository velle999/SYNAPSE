/*
 * cube.h — the desktop cube: a 3D workspace-switch transition for synui.
 *
 * anim.c animates WINDOWS; this animates the SCREEN. wlr_scene can translate a
 * node and set its opacity, and that is the whole of its vocabulary — there is
 * no way to ask it to draw the desk turned forty degrees about a vertical axis.
 * So the cube works the way effects.c does: it takes the frame, renders the
 * scene into a private offscreen buffer, and draws that buffer itself through a
 * GLES2 program — here one with a perspective vertex shader and two quads.
 *
 * The two faces are not two scenes. Only one desktop can be live in the scene
 * graph at a time, so cube_begin() PHOTOGRAPHS the outgoing desktop before the
 * switch touches anything, and every frame after that renders the incoming one
 * fresh. A still outgoing face is indistinguishable from a live one across a
 * few hundred milliseconds of rotation, and it costs one render instead of two
 * per frame.
 *
 * Every failure path leaves the animation off and returns false, so the caller
 * falls through to the ordinary commit: a cube that cannot compile its shader
 * is a desktop switch without an animation, never a black screen.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_CUBE_H
#define SYNUI_CUBE_H

#include <stdbool.h>
#include "synui.h"

/*
 * Start a turn on `o`, going in `dir` (+1 = to a higher-numbered desktop).
 *
 * ⚠ MUST BE CALLED BEFORE THE SWITCH HIDES ANYTHING. It snapshots the desktop
 * currently on screen, and a desktop whose windows have already been disabled
 * photographs as bare wallpaper. A no-op unless anim_workspace is `cube` with a
 * non-zero length — and a no-op, silently, if the snapshot cannot be taken, in
 * which case the switch is simply not animated.
 */
void cube_begin(syn_server_t *s, syn_output_t *o, int dir);

/* True while `o` is mid-turn — the frame path must take cube_output_commit()
 * rather than the CRT pass or the plain commit. */
bool cube_active(syn_output_t *o);

/* Draw one frame of the turn and commit it. False on any failure (and the turn
 * is cancelled), so the caller falls back to its ordinary commit path. */
bool cube_output_commit(syn_output_t *o);

/* Release this output's snapshot and swapchain (end of a turn, output destroy,
 * shutdown). Safe on an output that never turned. */
void cube_output_destroy(syn_output_t *o);

/* Drop the shared GL state (compositor shutdown). */
void cube_finish(syn_server_t *s);

#endif /* SYNUI_CUBE_H */
