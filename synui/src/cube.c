/*
 * cube.c — the desktop cube (anim_workspace = cube)
 *
 * WHY THIS IS NOT IN anim.c. Every other switch style moves WINDOWS: anim.c
 * offsets each frame node and sets its opacity, both of which wlr_scene offers.
 * A cube moves the SCREEN — the whole composited desktop, turned about a
 * vertical axis in perspective — and a scene node has no rotation, let alone a
 * projection. There is no arrangement of scene nodes that draws this.
 *
 * So the cube takes the frame instead, exactly the way effects.c does for the
 * CRT pass: the scene is rendered into a private offscreen swapchain, and that
 * buffer is drawn into the real output buffer by a GLES2 program of our own —
 * here a perspective vertex shader and two quads at right angles.
 *
 * ── The two faces, and why one of them is a photograph ──
 *
 * A cube shows two desktops at once and the scene graph holds one. Rather than
 * try to make wlr_scene render a desktop that is not the current one, cube_begin
 * PHOTOGRAPHS the outgoing desktop — one scene render into a buffer we keep —
 * at the instant before the switch hides it. Every frame after that renders the
 * incoming desktop live and pairs it with that still.
 *
 * The still is not a compromise anybody can see. The face carrying it is
 * rotating away from the viewer through a few hundred milliseconds; a video
 * player on it would drop those frames and nothing else would differ at all.
 * What it buys is a fixed cost: one extra render for the whole animation
 * instead of a second full scene render every frame.
 *
 * ⚠ THE ORDER IS THE FEATURE. cube_begin() must run BEFORE workspace_switch
 * disables anything — a desktop whose windows are already off photographs as
 * bare wallpaper, and the bug looks like "the cube works but the old side is
 * empty". layout.c calls it as its first act for exactly this reason.
 *
 * ── Failure is a missing animation, never a black screen ──
 *
 * Every step that can fail returns false and cancels the turn, and the frame
 * path falls through to the ordinary commit. A machine whose driver refuses the
 * shader gets desktop switches with no animation, which is a setting that did
 * nothing — not a compositor that cannot draw.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/render/egl.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include "fx_compat.h"
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "cube.h"

/* ── Shape of the turn ────────────────────────────────────── */
/*
 * The camera sits CUBE_DIST half-widths in front of the cube's centre. Smaller
 * is a wider lens: more perspective, more of the side face visible, and more of
 * the front face's corners thrown off screen. 2.8 is where the second desktop
 * is clearly a surface at an angle rather than a sheared rectangle, without the
 * near edge ballooning.
 */
#define CUBE_DIST  2.8f
#define CUBE_DIST_STR "2.8"
/*
 * ...and the desk pulls BACK as it turns, so a 90° cube fits on the screen it
 * is being drawn on. Without it the corners of both faces leave the viewport
 * through the middle of the turn and the effect reads as two rectangles sliding
 * rather than one solid turning. Zero at both ends by construction (sin 2|θ| is
 * 0 at 0 and at 90°), so the desktop at rest is pixel-exact either side of the
 * animation — which is the property that lets this run on every switch.
 */
#define CUBE_ZOOM  0.30f
#define CUBE_ZOOM_STR "0.30"
/*
 * How dark a face gets as it turns edge-on. Purely a lighting cue: without it
 * the two faces are equally bright and the corner between them disappears, so
 * the cube reads as flat. Both ends are 1.0 (a face square to the viewer is
 * never dimmed), so again the resting desktop is untouched.
 */
#define CUBE_SHADE 0.55f

/* ── GL state ─────────────────────────────────────────────── */
/*
 * Two programs for the same reason effects.c has two: a scene buffer may be
 * dmabuf-backed, which GLES2 samples through samplerExternalOES rather than
 * sampler2D, and the two need different shader source. Each face is its own
 * draw call sampling its own texture, so a turn whose snapshot is one kind and
 * whose live face is the other simply uses one program per face.
 */
struct syn_cube_gl {
    EGLDisplay dpy;
    EGLContext ctx;
    GLuint prog[2];                                   /* [0] 2D, [1] external */
    GLint  u_tex[2], u_theta[2], u_side[2], u_face[2];
    GLint  u_aspect[2], u_shade[2], u_swap[2];
    int    ready;      /* programs compiled; 0 until the first frame captures
                        * the fx_renderer's EGL context (see effects.c) */
    int    broken;     /* setup failed once — never try again, the failure is
                        * a property of the driver and retrying it every switch
                        * would log a shader error per desktop change */
};

/* Per-output turn. */
struct syn_cube {
    double  start;          /* CLOCK_MONOTONIC secs                          */
    double  dur;            /* seconds; anim_workspace_ms at the time it began*/
    float   side;           /* +1: the new desktop is the RIGHT face; -1 left */
    struct wlr_buffer  *snap;      /* the outgoing desktop, photographed      */
    struct wlr_texture *snap_tex;  /* ...as a texture, made once              */
    struct wlr_swapchain *chain;   /* offscreen the live face renders into    */
    int     from;           /* the desktop turned AWAY from, 1-based. The one
                             * turned TO is read at the end from the output
                             * itself: cube_begin runs before the switch lands
                             * and only knows the direction, not the target,
                             * which for a jump (2 -> 5) is not from + dir. */
    int     frames;         /* how many frames of the turn were actually drawn.
                             * 0 means the very first one failed, which is the
                             * difference between "the cube is off on this
                             * machine" and "it ran"; a turn that is not
                             * animating is otherwise indistinguishable from one
                             * that is, since both end on the same picture. */
};

/* syn_output_damage_whole() is static to synui_main.c, and only its first half
 * is wanted here anyway: this pass always commits the full output rect itself,
 * so all it needs is for the scene to actually re-render next frame. */
static void cube_damage(syn_output_t *o)
{
    if (o->scene_output)
        wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
}

static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── Shaders ──────────────────────────────────────────────── */
/*
 * One vertex shader draws both faces. `pos` is the face-local quad in [-1,1];
 * u_face selects which plane of the cube it is placed on:
 *
 *     front  (u_face 0):  p = ( x, y/aspect,  1 )
 *     side   (u_face 1):  p = ( side, y/aspect, -side*x )
 *
 * The side face hinges on the front face's edge in the direction of travel, so
 * the shared corner is a real corner: at x = side on the front face and at
 * z = 1 on the side face, the two quads meet at the same point for every θ.
 *
 * Working units are HALF THE SCREEN WIDTH, which is why y is divided by the
 * aspect on the way in and multiplied back out at the end: the cube is square
 * in horizontal cross-section (its depth equals its width, as a cube's must),
 * while the face keeps the screen's own proportions.
 *
 * The projection is chosen so that θ = 0 is the identity: f = CUBE_DIST - 1
 * makes the front face land exactly on the viewport, corner for corner. That
 * is what allows the cube to be armed on every switch — the first and last
 * frames of the animation are indistinguishable from no animation at all.
 */
static const char *vert_src =
    "attribute vec2 pos;\n"
    "uniform float u_theta;\n"
    "uniform float u_side;\n"
    "uniform float u_face;\n"    /* 0 = front (outgoing), 1 = side (incoming) */
    "uniform float u_aspect;\n"  /* SCREEN width / height, not the buffer's   */
    "uniform float u_swap;\n"    /* 1 on a 90/270 output — see below          */
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = pos * 0.5 + 0.5;\n"
    /* ⚠ THE AXIS IS THE VIEWER'S VERTICAL, NOT THE BUFFER'S. The scene renders
     * with the output transform already baked in, so on a monitor turned 90 or
     * 270 the buffer's x axis runs UP the screen — a cube built straight out of
     * buffer coordinates would turn about a horizontal axis there and roll like
     * a drum. Everything below therefore works in SCREEN coordinates, and the
     * result is mapped back on the last line.
     *
     * The swap is a reflection, so it reverses the handedness of the turn — and
     * that is exactly compensated by the display, which un-does the same swap on
     * the way to the panel. What the viewer sees is what the screen-space maths
     * says. At theta = 0 the two swaps cancel outright and the identity holds. */
    "    vec2 q = (u_swap > 0.5) ? pos.yx : pos.xy;\n"
    "    float ih = 1.0 / u_aspect;\n"
    "    vec3 p = (u_face < 0.5)\n"
    "           ? vec3(q.x, q.y * ih, 1.0)\n"
    "           : vec3(u_side, q.y * ih, -u_side * q.x);\n"
    /* Turn about the vertical axis. */
    "    float c = cos(u_theta), s = sin(u_theta);\n"
    "    vec3 r = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);\n"
    /* Perspective divide, then the pull-back that keeps 45° on screen. */
    "    float f  = " CUBE_DIST_STR " - 1.0;\n"
    "    float zc = " CUBE_DIST_STR " - r.z;\n"
    "    float k  = f / max(zc, 0.05);\n"
    "    float zoom = 1.0 - " CUBE_ZOOM_STR " * abs(sin(2.0 * u_theta));\n"
    "    vec2 P = vec2(r.x * k * zoom, r.y * u_aspect * k * zoom);\n"
    "    gl_Position = vec4((u_swap > 0.5) ? P.yx : P.xy, 0.0, 1.0);\n"
    "}\n";

/* %s is the sampler prelude, %s the sampler type — same two-variant trick as
 * effects.c. u_shade is the face's lighting, computed on the CPU because it is
 * one cosine per face per frame and the shader would recompute it per pixel. */
static const char *frag_fmt =
    "%s"
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform %s u_tex;\n"
    "uniform float u_shade;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.rgb * u_shade, 1.0);\n"
    "}\n";

/* ── EGL context juggling (see effects.c's header for why) ── */
struct egl_saved {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface draw, read;
};

static bool cube_make_current(struct syn_cube_gl *gl, struct egl_saved *save)
{
    save->dpy  = eglGetCurrentDisplay();
    save->ctx  = eglGetCurrentContext();
    save->draw = eglGetCurrentSurface(EGL_DRAW);
    save->read = eglGetCurrentSurface(EGL_READ);
    return eglMakeCurrent(gl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, gl->ctx);
}

static void cube_restore(struct egl_saved *save)
{
    if (save->dpy != EGL_NO_DISPLAY)
        eglMakeCurrent(save->dpy, save->draw, save->read, save->ctx);
    else
        eglMakeCurrent(eglGetCurrentDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "cube: shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint build_program(const char *prelude, const char *sampler)
{
    char frag[2048];
    int need = snprintf(frag, sizeof(frag), frag_fmt, prelude, sampler);
    if (need < 0 || (size_t)need >= sizeof(frag)) {
        /* snprintf truncates in silence and a shader cut mid-statement fails
         * to compile with an error pointing somewhere else entirely. */
        wlr_log(WLR_ERROR, "cube: shader source needs %d bytes, buffer is %zu",
                need, sizeof(frag));
        return 0;
    }

    GLuint vs = compile(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(prog, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "cube: program link failed: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* MUST run with the fx_renderer's EGL context current (the caller captures it
 * the way effects.c does — inside a throwaway render pass). */
static bool cube_gl_setup(struct syn_cube_gl *gl)
{
    gl->prog[0] = build_program("", "sampler2D");
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (exts && strstr(exts, "GL_OES_EGL_image_external"))
        gl->prog[1] = build_program(
            "#extension GL_OES_EGL_image_external : require\n",
            "samplerExternalOES");

    for (int i = 0; i < 2; i++) {
        if (!gl->prog[i]) continue;
        gl->u_tex[i]    = glGetUniformLocation(gl->prog[i], "u_tex");
        gl->u_theta[i]  = glGetUniformLocation(gl->prog[i], "u_theta");
        gl->u_side[i]   = glGetUniformLocation(gl->prog[i], "u_side");
        gl->u_face[i]   = glGetUniformLocation(gl->prog[i], "u_face");
        gl->u_aspect[i] = glGetUniformLocation(gl->prog[i], "u_aspect");
        gl->u_shade[i]  = glGetUniformLocation(gl->prog[i], "u_shade");
        gl->u_swap[i]   = glGetUniformLocation(gl->prog[i], "u_swap");
    }

    if (!gl->prog[0]) {
        wlr_log(WLR_ERROR, "cube: base program failed to compile");
        return false;
    }
    wlr_log(WLR_INFO, "cube: ready (external-oes: %s)",
            gl->prog[1] ? "yes" : "no");
    return true;
}

/* ── Offscreen scene render ───────────────────────────────── */
/*
 * Render the scene as it stands into `chain`, returning a LOCKED buffer the
 * caller owns. This is the whole of what makes both faces possible: the same
 * call photographs the outgoing desktop at cube_begin() and produces the live
 * incoming one every frame afterwards.
 *
 * Whole damage is forced first for the same reason effects.c forces it: the
 * face samples the ENTIRE buffer, so any region build_state leaves unpainted
 * shows whatever frame that rotating swapchain buffer last held.
 */
static struct wlr_buffer *cube_render_scene(syn_output_t *o,
                                            struct wlr_swapchain **chain)
{
    syn_server_t *s = o->server;
    struct wlr_output *wo = o->wlr_output;

    if (!wlr_output_configure_primary_swapchain(wo, NULL, &wo->swapchain))
        return NULL;

    int w = wo->swapchain->width, h = wo->swapchain->height;
    if (*chain && ((*chain)->width != w || (*chain)->height != h)) {
        wlr_swapchain_destroy(*chain);
        *chain = NULL;
    }
    if (!*chain) {
        *chain = wlr_swapchain_create(s->allocator, w, h,
                                      &wo->swapchain->format);
        if (!*chain) return NULL;
    }

    wlr_damage_ring_add_whole(&o->scene_output->damage_ring);

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    /* No colour transform on the way in, for effects.c's reason: it belongs on
     * the state that is actually committed, after our pass, not baked into a
     * buffer we are about to resample. */
    struct wlr_scene_output_state_options opts = { .swapchain = *chain };
    if (!wlr_scene_output_build_state(o->scene_output, &st, &opts)) {
        wlr_output_state_finish(&st);
        return NULL;
    }
    if (!(st.committed & WLR_OUTPUT_STATE_BUFFER)) {
        wlr_output_state_finish(&st);
        return NULL;
    }
    struct wlr_buffer *buf = wlr_buffer_lock(st.buffer);
    wlr_output_state_finish(&st);
    return buf;
}

/* ── Starting a turn ──────────────────────────────────────── */

static void cube_free(syn_output_t *o)
{
    struct syn_cube *cu = o->cube;
    if (!cu) return;
    o->cube = NULL;                 /* clear FIRST: cube_active() is asked from
                                     * the same frame path that can fail into
                                     * here, and a half-freed turn must never be
                                     * reachable */
    /* One line per turn, at DEBUG. INFO would put it beside every desktop
     * switch for the life of the session; DEBUG is where a `-d` run answers
     * "did it draw, and how much of the turn did it get". */
    wlr_log(WLR_DEBUG, "cube: %s %d->%d ended after %d frame(s) drawn",
            o->wlr_output ? o->wlr_output->name : "(gone)", cu->from,
            o->server ? output_workspace_index(o->server, o) + 1 : 0,
            cu->frames);

    if (cu->snap_tex) wlr_texture_destroy(cu->snap_tex);
    if (cu->snap)     wlr_buffer_unlock(cu->snap);
    if (cu->chain)    wlr_swapchain_destroy(cu->chain);
    free(cu);
}

void cube_begin(syn_server_t *s, syn_output_t *o, int dir)
{
    if (!s || !o || !o->scene_output) return;
    if (s->config.anim_workspace != ANIM_WS_CUBE) return;

    double dur = s->config.anim_workspace_ms / 1000.0;
    if (dur <= 0.0) return;         /* length 0 means "no animation", for every
                                     * style — the cube is not an exception */

    if (s->cube_gl && s->cube_gl->broken) return;

    /* A switch during a switch: the desk is already turning and the user has
     * pressed again. Drop the turn in flight and photograph what is on screen
     * now, which is a frame of the cube — turning from a cube into the next
     * cube is the honest picture of what just happened, and is what every
     * compositor that does this shows. */
    if (o->cube) cube_free(o);

    struct syn_cube *cu = calloc(1, sizeof(*cu));
    if (!cu) return;

    cu->snap = cube_render_scene(o, &cu->chain);
    if (!cu->snap) {
        /* No photograph, no cube. Silently: this is reachable on a frame where
         * the scene had nothing to render, and a desktop switch that logs an
         * error every time it happens to be quiet is worse than one that is
         * occasionally not animated. */
        if (cu->chain) wlr_swapchain_destroy(cu->chain);
        free(cu);
        return;
    }

    cu->start = now_secs();
    cu->dur   = dur;
    /* Going UP (dir > 0) brings the new desktop in from the RIGHT, matching the
     * slide style and the way a pager reads. */
    cu->side  = (dir >= 0) ? 1.0f : -1.0f;
    cu->from  = output_workspace_index(s, o) + 1;
    o->cube   = cu;

    /* The switch that called us is about to disable the outgoing windows, so
     * the very next frame must be ours. */
    cube_damage(o);
    wlr_output_schedule_frame(o->wlr_output);
}

bool cube_active(syn_output_t *o)
{
    return o && o->cube;
}

/* ── One frame of the turn ────────────────────────────────── */

/* The quad, as two triangles in face-local [-1,1]. */
static const GLfloat quad[12] = {
    -1.0f, -1.0f,   1.0f, -1.0f,  -1.0f,  1.0f,
    -1.0f,  1.0f,   1.0f, -1.0f,   1.0f,  1.0f,
};

static void cube_draw_face(struct syn_cube_gl *gl, struct wlr_texture *tex,
                           float theta, float side, float face, float aspect,
                           float swap, float shade)
{
    if (!tex) return;
    struct fx_texture_attribs ta;
    fx_texture_get_attribs(tex, &ta);
    int p = (ta.target == GL_TEXTURE_EXTERNAL_OES) ? 1 : 0;
    if (!gl->prog[p]) return;

    glUseProgram(gl->prog[p]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(ta.target, ta.tex);
    glTexParameteri(ta.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(ta.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(ta.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(ta.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(gl->u_tex[p], 0);
    glUniform1f(gl->u_theta[p],  theta);
    glUniform1f(gl->u_side[p],   side);
    glUniform1f(gl->u_face[p],   face);
    glUniform1f(gl->u_aspect[p], aspect);
    glUniform1f(gl->u_swap[p],   swap);
    glUniform1f(gl->u_shade[p],  shade);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindTexture(ta.target, 0);
}

bool cube_output_commit(syn_output_t *o)
{
    struct syn_cube *cu = o->cube;
    if (!cu) return false;

    syn_server_t *s = o->server;
    struct wlr_output *wo = o->wlr_output;

    /* Where we are in the turn, on the shared easing curve — a cube that
     * decayed differently from the window animations would read as a second
     * animation system (see anim.c). */
    double el = now_secs() - cu->start;
    float  t  = (cu->dur > 0.0) ? (float)(el / cu->dur) : 1.0f;
    if (t >= 1.0f) {
        /* Landed. Give the frame back to the ordinary path, which draws the
         * incoming desktop square to the viewer — which is precisely the last
         * frame this pass would have drawn, so there is no seam. */
        cube_free(o);
        cube_damage(o);
        return false;
    }
    float e = anim_curve_apply(s->config.anim_curve, t);

    /* Turn from 0 to a quarter, AWAY from the side the new desktop is on: the
     * right-hand face comes to the front by rotating the cube left. */
    float theta = -cu->side * e * (float)M_PI_2;

    /* The live face: the incoming desktop, rendered fresh this frame. */
    struct wlr_buffer *live_buf = cube_render_scene(o, &cu->chain);
    if (!live_buf) goto cancel;

    struct wlr_texture *live_tex = fx_texture_from_buffer(s->renderer, live_buf);
    if (!live_tex) { wlr_buffer_unlock(live_buf); goto cancel; }

    struct wlr_buffer *dst = wlr_swapchain_acquire(wo->swapchain);
    if (!dst) goto fail_live;

    GLuint fbo = fx_renderer_get_buffer_fbo(s->renderer, dst);
    if (!fbo) goto fail_dst;

    /* Capture the fx_renderer's EGL context and compile, once per session. The
     * throwaway pass is effects.c's trick and its header explains why there is
     * no other way to reach that context; dst is overwritten by our own raw-GL
     * pass immediately below, so an empty pass on it is harmless. */
    if (!s->cube_gl) {
        s->cube_gl = calloc(1, sizeof(*s->cube_gl));
        if (!s->cube_gl) goto fail_dst;
    }
    struct syn_cube_gl *gl = s->cube_gl;
    if (gl->broken) goto fail_dst;
    if (!gl->ready) {
        struct wlr_buffer_pass_options bpo = {0};
        struct wlr_render_pass *cap =
            wlr_renderer_begin_buffer_pass(s->renderer, dst, &bpo);
        if (!cap) goto fail_dst;
        gl->dpy = eglGetCurrentDisplay();
        gl->ctx = eglGetCurrentContext();
        bool ok = gl->dpy != EGL_NO_DISPLAY && gl->ctx != EGL_NO_CONTEXT &&
                  cube_gl_setup(gl);
        wlr_render_pass_submit(cap);
        if (!ok) {
            /* Once. Retrying per switch would log a driver's shader complaint
             * on every desktop change for the life of the session. */
            gl->broken = 1;
            wlr_log(WLR_ERROR, "cube: no usable GL pass — the cube style will "
                    "switch desktops without animating");
            goto fail_dst;
        }
        gl->ready = 1;
    }

    /* The photograph becomes a texture once, on the first frame of the turn —
     * not in cube_begin(), which runs inside a key handler where the renderer
     * has no pass open and nothing has yet established that this turn will
     * draw at all. It is kept for the whole turn: the buffer never changes. */
    if (!cu->snap_tex) {
        cu->snap_tex = fx_texture_from_buffer(s->renderer, cu->snap);
        if (!cu->snap_tex) goto fail_dst;
    }

    struct egl_saved saved;
    if (!cube_make_current(gl, &saved)) goto fail_dst;

    int w = wo->swapchain->width, h = wo->swapchain->height;

    /* A 90/270 output's buffer is the screen turned on its side, so the axis the
     * cube must turn about — and the aspect ratio the viewer sees — are both the
     * other way round. Flips leave the axis alone; this is the same test
     * effects.c makes for its scanlines, and for the same reason. */
    enum wl_output_transform tr = wo->transform;
    bool rot = (tr == WL_OUTPUT_TRANSFORM_90  ||
                tr == WL_OUTPUT_TRANSFORM_270 ||
                tr == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
                tr == WL_OUTPUT_TRANSFORM_FLIPPED_270);
    float swap   = rot ? 1.0f : 0.0f;
    float aspect = rot ? ((w > 0) ? (float)h / (float)w : 1.0f)
                       : ((h > 0) ? (float)w / (float)h : 1.0f);

    /* Face lighting from each face's normal, turned. Both reach 1.0 exactly at
     * the end of the turn they are square to the viewer for. */
    float c = cosf(theta), sn = sinf(theta);
    float shade_front = 1.0f - CUBE_SHADE * (1.0f - fabsf(c));
    float shade_side  = 1.0f - CUBE_SHADE * (1.0f - fabsf(sn));

    /* Painter's algorithm — there is no depth buffer on the output FBO, and
     * asking for one for two quads would be a swapchain format change. Draw the
     * face whose centre is FARTHER from the camera first. Front centre turns to
     * z = cos θ; the side face's centre to z = -side·sin θ. */
    float z_front = c;
    float z_side  = -cu->side * sn;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    /* The desk turns against black — there is nothing behind a cube, and the
     * corners of the viewport are genuinely empty for most of the turn. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (z_front <= z_side) {
        cube_draw_face(gl, cu->snap_tex, theta, cu->side, 0.0f, aspect,
                       swap, shade_front);
        cube_draw_face(gl, live_tex, theta, cu->side, 1.0f, aspect,
                       swap, shade_side);
    } else {
        cube_draw_face(gl, live_tex, theta, cu->side, 1.0f, aspect,
                       swap, shade_side);
        cube_draw_face(gl, cu->snap_tex, theta, cu->side, 0.0f, aspect,
                       swap, shade_front);
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* The buffer is scanned out by another context/device; the pass has to be
     * finished before it is committed. */
    glFinish();
    cube_restore(&saved);

    struct wlr_output_state st2;
    wlr_output_state_init(&st2);
    wlr_output_state_set_buffer(&st2, dst);
    pixman_region32_t full;
    pixman_region32_init_rect(&full, 0, 0, w, h);
    wlr_output_state_set_damage(&st2, &full);
    pixman_region32_fini(&full);

    /* Night light / colour filter / HDR, at scanout on top of our pass —
     * through the one call both other commit paths use, so a turning desk is
     * the same colour as a still one. */
    bool ok = hdr_commit(s, o, &st2);
    wlr_output_state_finish(&st2);

    wlr_buffer_unlock(dst);
    wlr_texture_destroy(live_tex);
    wlr_buffer_unlock(live_buf);

    if (!ok) goto cancel;

    cu->frames++;

    /* Keep the frames coming: the scene has no damage of its own between
     * switches, so without this the turn would stop on its first frame and the
     * desk would sit at an angle. */
    cube_damage(o);
    wlr_output_schedule_frame(wo);
    return true;

fail_dst:
    wlr_buffer_unlock(dst);
fail_live:
    wlr_texture_destroy(live_tex);
    wlr_buffer_unlock(live_buf);
cancel:
    /* Any failure ends the turn rather than retrying it next frame: a cube that
     * cannot draw must not hold the frame path hostage. */
    cube_free(o);
    cube_damage(o);
    return false;
}

void cube_output_destroy(syn_output_t *o)
{
    cube_free(o);
}

void cube_finish(syn_server_t *s)
{
    if (!s || !s->cube_gl) return;
    /* The programs die with the renderer's EGL context, exactly as effects.c's
     * do; there is nothing to delete here that outlives it. */
    free(s->cube_gl);
    s->cube_gl = NULL;
}
